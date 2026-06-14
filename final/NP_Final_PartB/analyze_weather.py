#!/usr/bin/env python3
"""
Analyze weather.csv collected by the C weather server.

Expected CSV format (no header):
    2026-06-11 10:30:20,26,57

Important assumption:
If duplicate readings are omitted by the server, the latest known reading is
treated as unchanged at every 10-second interval until the next stored record.
Use --extend-to-now only when the newest CSV row represents a currently active
sensor value; otherwise the chart should stay focused on the CSV time range.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


INTERVAL_SECONDS = 10
RECENT_SAMPLES = 30       # 30 samples * 10 seconds = recent 5 minutes
FORECAST_SAMPLES = 30     # 30 samples * 10 seconds = next 5 minutes


def load_and_expand_csv(csv_path: Path, extend_to_now: bool = False) -> pd.DataFrame:
    """Load sparse records and expand them into exact 10-second intervals."""
    if not csv_path.exists():
        raise FileNotFoundError(f"CSV file not found: {csv_path}")

    data = pd.read_csv(
        csv_path,
        header=None,
        names=["datetime", "temperature", "humidity"],
    )

    if data.empty:
        raise ValueError("CSV file is empty.")

    data["datetime"] = pd.to_datetime(data["datetime"], errors="coerce")
    data["temperature"] = pd.to_numeric(data["temperature"], errors="coerce")
    data["humidity"] = pd.to_numeric(data["humidity"], errors="coerce")
    data = data.dropna().sort_values("datetime")

    if data.empty:
        raise ValueError("CSV contains no valid weather records.")

    # Multiple records within the same 10-second slot:
    # keep the latest one in that slot.
    data["datetime"] = data["datetime"].dt.floor(f"{INTERVAL_SECONDS}s")
    data = data.drop_duplicates(subset="datetime", keep="last")
    data = data.set_index("datetime")

    start_time = data.index.min()
    end_time = data.index.max()

    # When requested, assume the newest value remains valid up to the current
    # 10-second boundary. Keep this off by default so old CSV files do not
    # stretch the x-axis across days.
    if extend_to_now:
        current_slot = pd.Timestamp.now().floor(f"{INTERVAL_SECONDS}s")
        if current_slot > end_time:
            end_time = current_slot

    full_timeline = pd.date_range(
        start=start_time,
        end=end_time,
        freq=f"{INTERVAL_SECONDS}s",
    )

    # Reindex creates the missing 10-second rows.
    # ffill propagates the latest known reading until the next new record.
    expanded = data.reindex(full_timeline).ffill()
    expanded.index.name = "datetime"

    return expanded


def linear_trend(
    values: pd.Series,
    recent_samples: int,
    forecast_samples: int,
) -> tuple[np.ndarray, np.ndarray, float]:
    """
    Fit a line to the latest samples.

    Returns:
        fitted_recent: fitted values for recent observations
        forecast: predicted values for future intervals
        slope_per_minute: estimated change per minute
    """
    recent = values.tail(recent_samples).astype(float)
    x = np.arange(len(recent), dtype=float)

    if len(recent) < 2 or recent.nunique() == 1:
        slope_per_sample = 0.0
        intercept = float(recent.iloc[-1])
    else:
        intercept, slope_per_sample = np.polynomial.polynomial.polyfit(
            x,
            recent.to_numpy(),
            deg=1,
        )

    fitted_recent = intercept + slope_per_sample * x

    future_x = np.arange(
        len(recent),
        len(recent) + forecast_samples,
        dtype=float,
    )
    forecast = intercept + slope_per_sample * future_x

    slope_per_minute = slope_per_sample * (60 / INTERVAL_SECONDS)
    return fitted_recent, forecast, slope_per_minute


def describe_trend(slope_per_minute: float, threshold: float) -> str:
    """Convert a numeric slope into a simple trend label."""
    if slope_per_minute > threshold:
        return "rising"
    if slope_per_minute < -threshold:
        return "falling"
    return "stable"


def plot_weather(data: pd.DataFrame, output_path: Path) -> None:
    recent_count = min(RECENT_SAMPLES, len(data))
    recent = data.tail(recent_count)

    temp_fit, temp_forecast, temp_slope = linear_trend(
        data["temperature"],
        recent_count,
        FORECAST_SAMPLES,
    )
    humid_fit, humid_forecast, humid_slope = linear_trend(
        data["humidity"],
        recent_count,
        FORECAST_SAMPLES,
    )

    future_times = pd.date_range(
        start=data.index[-1] + pd.Timedelta(seconds=INTERVAL_SECONDS),
        periods=FORECAST_SAMPLES,
        freq=f"{INTERVAL_SECONDS}s",
    )

    figure, axes = plt.subplots(2, 1, figsize=(11, 8), sharex=True)

    axes[0].plot(data.index, data["temperature"], label="Expanded temperature")
    axes[0].plot(recent.index, temp_fit, linestyle="--", label="Recent linear trend")
    axes[0].plot(future_times, temp_forecast, linestyle=":", label="Short-term estimate")
    axes[0].set_ylabel("Temperature (°C)")
    axes[0].set_title(
        f"Temperature: {describe_trend(temp_slope, 0.05)} "
        f"({temp_slope:+.3f} °C/min)"
    )
    axes[0].legend()
    axes[0].grid(True)

    axes[1].plot(data.index, data["humidity"], label="Expanded humidity")
    axes[1].plot(recent.index, humid_fit, linestyle="--", label="Recent linear trend")
    axes[1].plot(future_times, humid_forecast, linestyle=":", label="Short-term estimate")
    axes[1].set_ylabel("Humidity (%)")
    axes[1].set_xlabel("Time")
    axes[1].set_title(
        f"Humidity: {describe_trend(humid_slope, 0.2)} "
        f"({humid_slope:+.3f} %/min)"
    )
    axes[1].legend()
    axes[1].grid(True)

    date_locator = mdates.AutoDateLocator(minticks=4, maxticks=8)
    axes[1].xaxis.set_major_locator(date_locator)
    axes[1].xaxis.set_major_formatter(mdates.ConciseDateFormatter(date_locator))
    figure.autofmt_xdate()
    figure.tight_layout()
    figure.savefig(output_path, dpi=160)

    print(f"Expanded samples: {len(data)}")
    print(f"Expanded time range: {data.index[0]} to {data.index[-1]}")
    print(
        f"Temperature trend: {describe_trend(temp_slope, 0.05)} "
        f"({temp_slope:+.3f} °C/min)"
    )
    print(
        f"Humidity trend: {describe_trend(humid_slope, 0.2)} "
        f"({humid_slope:+.3f} %/min)"
    )
    print(f"Chart saved to: {output_path}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Expand and analyze weather.csv at 10-second intervals."
    )
    parser.add_argument(
        "csv",
        nargs="?",
        default="weather.csv",
        help="Path to weather.csv (default: weather.csv)",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="weather_analysis.png",
        help="Output chart path (default: weather_analysis.png)",
    )
    parser.add_argument(
        "--extend-to-now",
        action="store_true",
        help="Fill unchanged readings from the last CSV row up to the current time.",
    )
    args = parser.parse_args()

    data = load_and_expand_csv(Path(args.csv), extend_to_now=args.extend_to_now)
    plot_weather(data, Path(args.output))


if __name__ == "__main__":
    main()
