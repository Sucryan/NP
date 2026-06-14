// analyze_weather.cpp
// C++17, standard library only.
// Reads weather.csv, expands omitted duplicate readings into 10-second samples,
// computes recent linear trends, and writes an SVG chart.
//
// Compile:
//   g++ -std=c++17 -O2 analyze_weather.cpp -o analyze_weather
//
// Run:
//   ./analyze_weather weather.csv
//   ./analyze_weather weather.csv -o weather_analysis.svg
//   ./analyze_weather weather.csv --extend-to-now

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static const int INTERVAL_SECONDS = 10;
static const int RECENT_SAMPLES = 30;
static const int FORECAST_SAMPLES = 30;

struct Reading {
    std::time_t timestamp;
    double temperature;
    double humidity;
};

struct TrendResult {
    std::vector<double> fitted_recent;
    std::vector<double> forecast;
    double slope_per_minute = 0.0;
};

struct ProgramOptions {
    std::string csv_path = "weather.csv";
    std::string output_path = "weather_analysis.svg";
    bool extend_to_now = false;
};

std::string format_time(std::time_t timestamp) {
    std::tm *local = std::localtime(&timestamp);
    if (!local) return "(invalid time)";
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local);
    return std::string(buffer);
}

std::time_t parse_datetime(const std::string &text) {
    std::tm value{};
    std::istringstream input(text);
    input >> std::get_time(&value, "%Y-%m-%d %H:%M:%S");
    if (input.fail()) return static_cast<std::time_t>(-1);
    value.tm_isdst = -1;
    return std::mktime(&value);
}

std::time_t floor_to_interval(std::time_t timestamp) {
    return timestamp - (timestamp % INTERVAL_SECONDS);
}

bool parse_csv_line(const std::string &line, Reading &out) {
    std::string datetime_part, temp_part, humid_part;
    std::stringstream ss(line);

    if (!std::getline(ss, datetime_part, ',')) return false;
    if (!std::getline(ss, temp_part, ',')) return false;
    if (!std::getline(ss, humid_part, ',')) return false;

    std::time_t timestamp = parse_datetime(datetime_part);
    if (timestamp == static_cast<std::time_t>(-1)) return false;

    try {
        out.timestamp = floor_to_interval(timestamp);
        out.temperature = std::stod(temp_part);
        out.humidity = std::stod(humid_part);
    } catch (...) {
        return false;
    }

    return true;
}

std::vector<Reading> load_and_expand_csv(const std::string &csv_path, bool extend_to_now) {
    std::ifstream file(csv_path);
    if (!file) {
        throw std::runtime_error("CSV file not found: " + csv_path);
    }

    // Sorted by time. If multiple rows fall into the same 10-second slot,
    // the later row overwrites the earlier one.
    std::map<std::time_t, Reading> sparse_data;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Reading reading{};
        if (parse_csv_line(line, reading)) {
            sparse_data[reading.timestamp] = reading;
        }
    }

    if (sparse_data.empty()) {
        throw std::runtime_error("CSV contains no valid weather records.");
    }

    std::time_t start_time = sparse_data.begin()->first;
    std::time_t end_time = sparse_data.rbegin()->first;

    if (extend_to_now) {
        std::time_t now_slot = floor_to_interval(std::time(nullptr));
        if (now_slot > end_time) end_time = now_slot;
    }

    std::vector<Reading> expanded;
    Reading latest = sparse_data.begin()->second;

    for (std::time_t t = start_time; t <= end_time; t += INTERVAL_SECONDS) {
        auto found = sparse_data.find(t);
        if (found != sparse_data.end()) {
            latest = found->second;
        }

        Reading item = latest;
        item.timestamp = t;
        expanded.push_back(item);
    }

    return expanded;
}

TrendResult linear_trend(const std::vector<double> &values, int recent_samples, int forecast_samples) {
    TrendResult result;
    if (values.empty()) return result;

    int count = std::min<int>(recent_samples, static_cast<int>(values.size()));
    int start = static_cast<int>(values.size()) - count;

    std::vector<double> recent;
    for (int i = start; i < static_cast<int>(values.size()); ++i) {
        recent.push_back(values[i]);
    }

    double slope = 0.0;
    double intercept = recent.back();

    bool all_same = true;
    for (double v : recent) {
        if (std::fabs(v - recent.front()) > 1e-9) {
            all_same = false;
            break;
        }
    }

    if (recent.size() >= 2 && !all_same) {
        double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;

        for (int i = 0; i < static_cast<int>(recent.size()); ++i) {
            double x = static_cast<double>(i);
            double y = recent[i];
            sum_x += x;
            sum_y += y;
            sum_xx += x * x;
            sum_xy += x * y;
        }

        double n = static_cast<double>(recent.size());
        double denom = n * sum_xx - sum_x * sum_x;

        if (std::fabs(denom) > 1e-12) {
            slope = (n * sum_xy - sum_x * sum_y) / denom;
            intercept = (sum_y - slope * sum_x) / n;
        }
    }

    for (int i = 0; i < static_cast<int>(recent.size()); ++i) {
        result.fitted_recent.push_back(intercept + slope * i);
    }

    for (int i = static_cast<int>(recent.size());
         i < static_cast<int>(recent.size()) + forecast_samples;
         ++i) {
        result.forecast.push_back(intercept + slope * i);
    }

    result.slope_per_minute = slope * (60.0 / INTERVAL_SECONDS);
    return result;
}

std::string describe_trend(double slope_per_minute, double threshold) {
    if (slope_per_minute > threshold) return "rising";
    if (slope_per_minute < -threshold) return "falling";
    return "stable";
}

std::string points_to_svg(const std::vector<std::pair<double, double>> &points) {
    std::ostringstream out;
    for (const auto &p : points) {
        out << std::fixed << std::setprecision(2) << p.first << "," << p.second << " ";
    }
    return out.str();
}

class Mapper {
public:
    Mapper(double left, double top, double width, double height,
           double min_x, double max_x, double min_y, double max_y)
        : left_(left), top_(top), width_(width), height_(height),
          min_x_(min_x), max_x_(max_x), min_y_(min_y), max_y_(max_y) {}

    double x(double value) const {
        if (std::fabs(max_x_ - min_x_) < 1e-9) return left_;
        return left_ + (value - min_x_) / (max_x_ - min_x_) * width_;
    }

    double y(double value) const {
        if (std::fabs(max_y_ - min_y_) < 1e-9) return top_ + height_ / 2.0;
        return top_ + height_ - (value - min_y_) / (max_y_ - min_y_) * height_;
    }

private:
    double left_, top_, width_, height_, min_x_, max_x_, min_y_, max_y_;
};

std::pair<double, double> minmax_with_padding(const std::vector<double> &a,
                                              const std::vector<double> &b,
                                              const std::vector<double> &c) {
    double mn = std::numeric_limits<double>::infinity();
    double mx = -std::numeric_limits<double>::infinity();

    auto consume = [&](const std::vector<double> &values) {
        for (double v : values) {
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
    };

    consume(a);
    consume(b);
    consume(c);

    if (!std::isfinite(mn) || !std::isfinite(mx)) {
        return {0.0, 1.0};
    }

    if (std::fabs(mx - mn) < 1e-9) {
        return {mn - 1.0, mx + 1.0};
    }

    double padding = (mx - mn) * 0.12;
    return {mn - padding, mx + padding};
}

void write_svg_chart(const std::vector<Reading> &data,
                     const TrendResult &temp_trend,
                     const TrendResult &humid_trend,
                     const std::string &output_path) {
    const double width = 1100.0;
    const double height = 800.0;
    const double left = 80.0;
    const double panel_width = 940.0;
    const double panel_height = 250.0;
    const double temp_top = 80.0;
    const double humid_top = 440.0;

    std::time_t start_time = data.front().timestamp;
    std::time_t last_data_time = data.back().timestamp;
    std::time_t final_time = last_data_time + FORECAST_SAMPLES * INTERVAL_SECONDS;

    double min_x = 0.0;
    double max_x = static_cast<double>(final_time - start_time);

    int recent_count = std::min<int>(RECENT_SAMPLES, static_cast<int>(data.size()));
    int recent_start = static_cast<int>(data.size()) - recent_count;

    std::vector<double> temp_values, humid_values;
    for (const Reading &r : data) {
        temp_values.push_back(r.temperature);
        humid_values.push_back(r.humidity);
    }

    auto temp_range = minmax_with_padding(temp_values, temp_trend.fitted_recent, temp_trend.forecast);
    auto humid_range = minmax_with_padding(humid_values, humid_trend.fitted_recent, humid_trend.forecast);

    Mapper temp_map(left, temp_top, panel_width, panel_height, min_x, max_x, temp_range.first, temp_range.second);
    Mapper humid_map(left, humid_top, panel_width, panel_height, min_x, max_x, humid_range.first, humid_range.second);

    std::vector<std::pair<double, double>> temp_raw, humid_raw, temp_fit, humid_fit, temp_forecast, humid_forecast;

    for (const Reading &r : data) {
        double sec = static_cast<double>(r.timestamp - start_time);
        temp_raw.push_back({temp_map.x(sec), temp_map.y(r.temperature)});
        humid_raw.push_back({humid_map.x(sec), humid_map.y(r.humidity)});
    }

    for (int i = 0; i < recent_count; ++i) {
        int idx = recent_start + i;
        double sec = static_cast<double>(data[idx].timestamp - start_time);
        temp_fit.push_back({temp_map.x(sec), temp_map.y(temp_trend.fitted_recent[i])});
        humid_fit.push_back({humid_map.x(sec), humid_map.y(humid_trend.fitted_recent[i])});
    }

    for (int i = 0; i < FORECAST_SAMPLES; ++i) {
        std::time_t t = last_data_time + (i + 1) * INTERVAL_SECONDS;
        double sec = static_cast<double>(t - start_time);
        temp_forecast.push_back({temp_map.x(sec), temp_map.y(temp_trend.forecast[i])});
        humid_forecast.push_back({humid_map.x(sec), humid_map.y(humid_trend.forecast[i])});
    }

    std::ofstream svg(output_path);
    if (!svg) {
        throw std::runtime_error("Cannot open output file: " + output_path);
    }

    auto slope_text = [](double value, const std::string &unit) {
        std::ostringstream out;
        out << std::showpos << std::fixed << std::setprecision(3) << value << " " << unit;
        return out.str();
    };

    std::string temp_title = "Temperature: " + describe_trend(temp_trend.slope_per_minute, 0.05) +
        " (" + slope_text(temp_trend.slope_per_minute, "C/min") + ")";
    std::string humid_title = "Humidity: " + describe_trend(humid_trend.slope_per_minute, 0.2) +
        " (" + slope_text(humid_trend.slope_per_minute, "%/min") + ")";

    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << " " << height << "\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    svg << "<style>"
        << "text{font-family:Arial,sans-serif;fill:#222}.axis{stroke:#444;stroke-width:1}"
        << ".grid{stroke:#ddd;stroke-width:1}.raw{fill:none;stroke:#2563eb;stroke-width:2}"
        << ".fit{fill:none;stroke:#dc2626;stroke-width:2;stroke-dasharray:8 5}"
        << ".forecast{fill:none;stroke:#16a34a;stroke-width:2;stroke-dasharray:3 5}"
        << ".title{font-size:20px;font-weight:bold}.small{font-size:12px}.legend{font-size:14px}"
        << "</style>\n";

    auto draw_panel = [&](double top, const std::string &title, const std::string &ylabel,
                          const std::pair<double, double> &range,
                          const std::vector<std::pair<double, double>> &raw,
                          const std::vector<std::pair<double, double>> &fit,
                          const std::vector<std::pair<double, double>> &forecast) {
        svg << "<text x=\"" << left << "\" y=\"" << top - 30 << "\" class=\"title\">" << title << "</text>\n";
        svg << "<rect x=\"" << left << "\" y=\"" << top << "\" width=\"" << panel_width
            << "\" height=\"" << panel_height << "\" fill=\"none\" class=\"axis\"/>\n";

        for (int i = 1; i < 5; ++i) {
            double y = top + panel_height * i / 5.0;
            svg << "<line x1=\"" << left << "\" y1=\"" << y << "\" x2=\"" << left + panel_width
                << "\" y2=\"" << y << "\" class=\"grid\"/>\n";
        }

        for (int i = 0; i <= 5; ++i) {
            double value = range.first + (range.second - range.first) * i / 5.0;
            double y = top + panel_height - panel_height * i / 5.0;
            svg << "<text x=\"" << left - 10 << "\" y=\"" << y + 4
                << "\" text-anchor=\"end\" class=\"small\">" << std::fixed << std::setprecision(1)
                << value << "</text>\n";
        }

        svg << "<text x=\"25\" y=\"" << top + panel_height / 2
            << "\" transform=\"rotate(-90 25 " << top + panel_height / 2
            << ")\" text-anchor=\"middle\" class=\"small\">" << ylabel << "</text>\n";

        svg << "<polyline class=\"raw\" points=\"" << points_to_svg(raw) << "\"/>\n";
        svg << "<polyline class=\"fit\" points=\"" << points_to_svg(fit) << "\"/>\n";
        svg << "<polyline class=\"forecast\" points=\"" << points_to_svg(forecast) << "\"/>\n";
    };

    draw_panel(temp_top, temp_title, "Temperature (C)", temp_range, temp_raw, temp_fit, temp_forecast);
    draw_panel(humid_top, humid_title, "Humidity (%)", humid_range, humid_raw, humid_fit, humid_forecast);

    for (int i = 0; i <= 5; ++i) {
        double ratio = i / 5.0;
        std::time_t t = start_time + static_cast<std::time_t>((final_time - start_time) * ratio);
        double x = left + panel_width * ratio;
        std::string label = format_time(t).substr(11);

        svg << "<line x1=\"" << x << "\" y1=\"" << humid_top + panel_height
            << "\" x2=\"" << x << "\" y2=\"" << humid_top + panel_height + 6
            << "\" class=\"axis\"/>\n";
        svg << "<text x=\"" << x << "\" y=\"" << humid_top + panel_height + 24
            << "\" text-anchor=\"middle\" class=\"small\">" << label << "</text>\n";
    }

    svg << "<text x=\"" << left << "\" y=\"730\" class=\"legend\">"
        << "<tspan fill=\"#2563eb\">Expanded data</tspan> | "
        << "<tspan fill=\"#dc2626\">Recent linear trend</tspan> | "
        << "<tspan fill=\"#16a34a\">Short-term estimate</tspan>"
        << "</text>\n";

    svg << "<text x=\"" << left << "\" y=\"755\" class=\"small\">"
        << "Expanded time range: " << format_time(data.front().timestamp)
        << " to " << format_time(data.back().timestamp)
        << ". Duplicate readings are expanded every " << INTERVAL_SECONDS
        << " seconds by carrying forward the latest value."
        << "</text>\n";

    svg << "</svg>\n";
}

ProgramOptions parse_arguments(int argc, char **argv) {
    ProgramOptions options;
    bool csv_seen = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--extend-to-now") {
            options.extend_to_now = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing output path after " + arg);
            }
            options.output_path = argv[++i];
        } else if (!csv_seen) {
            options.csv_path = arg;
            csv_seen = true;
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    return options;
}

int main(int argc, char **argv) {
    try {
        ProgramOptions options = parse_arguments(argc, argv);
        std::vector<Reading> data = load_and_expand_csv(options.csv_path, options.extend_to_now);

        std::vector<double> temperatures, humidities;
        for (const Reading &r : data) {
            temperatures.push_back(r.temperature);
            humidities.push_back(r.humidity);
        }

        int recent_count = std::min<int>(RECENT_SAMPLES, static_cast<int>(data.size()));
        TrendResult temp_trend = linear_trend(temperatures, recent_count, FORECAST_SAMPLES);
        TrendResult humid_trend = linear_trend(humidities, recent_count, FORECAST_SAMPLES);

        write_svg_chart(data, temp_trend, humid_trend, options.output_path);

        std::cout << "Expanded samples: " << data.size() << "\n";
        std::cout << "Expanded time range: " << format_time(data.front().timestamp)
                  << " to " << format_time(data.back().timestamp) << "\n";

        std::cout << "Temperature trend: "
                  << describe_trend(temp_trend.slope_per_minute, 0.05)
                  << " (" << std::showpos << std::fixed << std::setprecision(3)
                  << temp_trend.slope_per_minute << std::noshowpos << " C/min)\n";

        std::cout << "Humidity trend: "
                  << describe_trend(humid_trend.slope_per_minute, 0.2)
                  << " (" << std::showpos << std::fixed << std::setprecision(3)
                  << humid_trend.slope_per_minute << std::noshowpos << " %/min)\n";

        std::cout << "Chart saved to: " << options.output_path << "\n";
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
