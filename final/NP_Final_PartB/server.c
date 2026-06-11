#include "sock_compat.h"
#include <time.h>
const char *get_content_type(const char* path)
{
    char *extension[] = {
        ".css",
        ".csv",
        ".gif",
        ".htm",
        ".html",
        ".ico",
        ".jpeg",
        ".jpg",
        ".js",
        ".json",
        ".png",
        ".pdf",
        ".svg",
        ".txt"
    };

    char *mime_type[] = {
        "text/css",
        "text/csv",
        "image/gif",
        "text/html",
        "text/html",
        "image/x-icon",
        "image/jpeg",
        "image/jpeg",
        "application/javascript",
        "application/json",
        "image/png",
        "application/pdf",
        "image/svg+xml",
        "text/plain"
    };

    const char *last_dot = strrchr(path, '.');
    if (last_dot)
    {
        for (int i = 0; i < sizeof(extension) / sizeof(char*); i++)
        {
            if (strcmp(last_dot, extension[i]) == 0)
            {
                return mime_type[i];
            }
        }
    }
    return "application/octet-stream";
}

SOCKET create_socket(const char* host, const char *port)
{
    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *bind_address;
    getaddrinfo(host, port, &hints, &bind_address);

    printf("Creating socket...\n");
    SOCKET socket_listen;

    socket_listen = socket(
        bind_address->ai_family,
        bind_address->ai_socktype,
        bind_address->ai_protocol
    );
    if (!ISVALIDSOCKET(socket_listen))
    {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    int option = 0;
    if (setsockopt(socket_listen, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&option, sizeof(option))) {
        fprintf(stderr, "setsockopt() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }

    printf("Binding socket to local address...\n");
    int bind_result = bind (
        socket_listen,
        bind_address->ai_addr,
        bind_address->ai_addrlen
    );

    if (bind_result)
    {
        fprintf(stderr, "bind() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    freeaddrinfo(bind_address);

    printf("Listening...\n");
    int listen_result = listen(socket_listen, 10);
    if (listen_result < 0)
    {
        fprintf(stderr, "listen() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    return socket_listen;
}

#define MAX_REQUEST_SIZE 2047
struct client_info
{
    socklen_t address_length;
    struct sockaddr_storage address;
    SOCKET socket;
    char request[MAX_REQUEST_SIZE + 1];
    int received;
    struct client_info *next;
};

static struct client_info *client_list = 0;

struct client_info *get_client (SOCKET query_socket)
{
    struct client_info *client = client_list;

    while (client)
    {
        if (client->socket == query_socket)
        {
            break;
        }
        client = client->next;
    }

    if (client)
    {
        return client;
    }

    /*If no client could be found within the client list, 
     *It means new client, need to connect to the list.
     */

     struct client_info *new_client = (struct client_info*) calloc(1, sizeof(struct client_info));

     if (!new_client)
     {
        fprintf(stderr, "Out of memory, cannot create new client.");
        exit(1);
     }

     new_client->address_length = sizeof(new_client->address);
     new_client->next = client_list;
     client_list = new_client;
     return new_client;

}

void drop_client (struct client_info *to_drop_client)
{
    CLOSESOCKET(to_drop_client->socket);
    struct client_info **client_list_pointer = &client_list;

    while (*client_list_pointer)
    {
        if (*client_list_pointer == to_drop_client)
        {
            *client_list_pointer = to_drop_client->next;
            free(to_drop_client);
            return;
        }
        client_list_pointer = &(*client_list_pointer)->next;
    }

    fprintf(stderr, "drop_client not found.\n");
    exit(1);
}

const char *get_client_address(struct client_info *client)
{
    static char address_literal[128];
    getnameinfo(
        (struct sockaddr*) &client->address,
        client->address_length,
        address_literal,
        sizeof(address_literal),
        0,
        0,
        NI_NUMERICHOST
    );

    return address_literal;
}

fd_set wait_on_clients(SOCKET server_socket)
{
    fd_set read_ready;
    FD_ZERO(&read_ready);
    FD_SET(server_socket, &read_ready);
    SOCKET max_socket = server_socket;

    struct client_info *client = client_list;

    while(client)
    {
        FD_SET(client->socket, &read_ready);
        if (client->socket > max_socket)
        {
            max_socket = client->socket;
        }
        client = client->next;
    }

    int select_result = select(
        max_socket + 1,
        &read_ready,
        0,
        0,
        0
    );

    if (select_result < 0)
    {
        fprintf(stderr, "select() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    return read_ready;
}

void send_status_code
(
    struct client_info *to_send_client,
    int status_code,
    char *additional_header,
    char *additional_content
)
{
    char to_send_response[1024];
    memset(to_send_response, 0, sizeof(to_send_response));
    switch (status_code)
    {
        case 400:
            sprintf(to_send_response + strlen(to_send_response), "HTTP/1.1 %d Bad Request\r\n", status_code);
            sprintf(to_send_response + strlen(to_send_response), "Connection: close\r\n");
            if (additional_content == 0)
            {
                sprintf(to_send_response + strlen(to_send_response), "Content-Length: %d\r\n", strlen("Bad Request"));
                sprintf(to_send_response + strlen(to_send_response), "\r\n");
                sprintf(to_send_response + strlen(to_send_response), "Bad Request");
            }
            else
            {
                sprintf(to_send_response + strlen(to_send_response), "Content-Length: %d\r\n", strlen(additional_content));
                sprintf(to_send_response + strlen(to_send_response), "\r\n");
                sprintf(to_send_response + strlen(to_send_response), additional_content);
            }
            break;
        case 404:
            sprintf(to_send_response + strlen(to_send_response), "HTTP/1.1 %d Not Found\r\n", status_code);
            sprintf(to_send_response + strlen(to_send_response), "Connection: close\r\n");
            if (additional_content == 0)
            {
                sprintf(to_send_response + strlen(to_send_response), "Content-Length: %d\r\n", strlen("Not Found"));
                sprintf(to_send_response + strlen(to_send_response), "\r\n");
                sprintf(to_send_response + strlen(to_send_response), "Not Found");
            }
            else
            {
                sprintf(to_send_response + strlen(to_send_response), "Content-Length: %d\r\n", strlen(additional_content));
                sprintf(to_send_response + strlen(to_send_response), "\r\n");
                sprintf(to_send_response + strlen(to_send_response), additional_content);
            }
            break;
        case 500:
            sprintf(to_send_response + strlen(to_send_response), "HTTP/1.1 %d Internal Server Error\r\n", status_code);
            sprintf(to_send_response + strlen(to_send_response), "Connection: close\r\n");
            if (additional_content == 0)
            {
                sprintf(to_send_response + strlen(to_send_response), "Content-Length: %d\r\n", strlen("Internal Server Error"));
                sprintf(to_send_response + strlen(to_send_response), "\r\n");
                sprintf(to_send_response + strlen(to_send_response), "Internal Server Error");
            }
            else
            {
                sprintf(to_send_response + strlen(to_send_response), "Content-Length: %d\r\n", strlen(additional_content));
                sprintf(to_send_response + strlen(to_send_response), "\r\n");
                sprintf(to_send_response + strlen(to_send_response), additional_content);
            }
            break;
        // 額外新增一個200，用來reply給Arduino
        case 200:
            sprintf(
                to_send_response + strlen(to_send_response),
                "HTTP/1.1 200 OK\r\n"
            );

            sprintf(
                to_send_response + strlen(to_send_response),
                "Connection: close\r\n"
            );

            if (additional_content == 0)
            {
                additional_content = "OK";
            }

            sprintf(
                to_send_response + strlen(to_send_response),
                "Content-Length: %zu\r\n",
                strlen(additional_content)
            );

            sprintf(
                to_send_response + strlen(to_send_response),
                "\r\n"
            );

            sprintf(
                to_send_response + strlen(to_send_response),
                "%s",
                additional_content
            );
            break;
        default:
            sprintf(to_send_response + strlen(to_send_response), "HTTP/1.1 500 Internal Server Error\r\n");
            sprintf(to_send_response + strlen(to_send_response), "Connection: close\r\n");
            sprintf(to_send_response + strlen(to_send_response), "Content-Length: %d\r\n", strlen("Server issued an unknown status code."));
            sprintf(to_send_response + strlen(to_send_response), "\r\n");
            sprintf(to_send_response + strlen(to_send_response), "Server issued an unknown status code.");
            fprintf(stderr, "Unknown status code (%d)\n", status_code);
            break;
    }
    send(
        to_send_client->socket,
        to_send_response,
        strlen(to_send_response), 
        0
    );
    drop_client(to_send_client);
}

#define RESPONSE_BUFFER_SIZE 1024
void send_resource
(
    struct client_info *client,
    const char *path
)
{
    printf("send_resource %s %s\n", get_client_address(client), path);

    /* If it ends with / then try to find index.html */
    if (strcmp(path, "/") == 0)
    {
        path = "/index.html";
    }

    /* This is to prevent super long request. */
    if (strlen(path) > 128)
    {
        send_status_code(client, 400, 0, 0);
        return;
    }

    /* This is to prevent directory escape and triversal */
    if (strstr(path, ".."))
    {
        send_status_code(client, 404, 0, 0);
        return;
    }

    /* Theoretically, path can be as long as to OSes' define.*/
    /* However, in order to keep backward compatibility, many OSes */
    /* Still applying soft limit of 256 char length in file path. */
    char full_path[256];

    /* In our implementation, we only only to serve the content within */
    /* The folder public. Anything outside the public folder will be ignored.*/
    sprintf(full_path, "public%s", path);

#if defined(_WIN32)

    /* Unfortunately, this is the differences between Windows and POSIX OS */
    /* Within Windows's file system path, it uses \ as path deliminator, it's */
    /* It's very different from URL and POSIX OSes that uses /, so the HTTP path*/
    /* Must be convereted into Windows's form before we can use this path to  access*/
    /* The underlying files. */
    char *full_path_pointer = full_path;
    while (*full_path_pointer)
    {
        if (*full_path_pointer == '/')
        {
            *full_path_pointer == '\\';
        }
        full_path_pointer++;
    }
#endif

    FILE *file_pointer = fopen(full_path, "rb");

    if (!file_pointer)
    {
        send_status_code(client, 404, 0, 0);
        return;
    }

    fseek(file_pointer, 0L, SEEK_END);
    size_t file_size = ftell(file_pointer);
    rewind(file_pointer);

    const char *content_type = get_content_type(full_path);

    /* Start forming answering section. */
    char response_buffer[RESPONSE_BUFFER_SIZE];

    sprintf(response_buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    sprintf(response_buffer, "Connection: close\r\n");
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    sprintf(response_buffer, "Content-Length: %u\r\n", file_size);
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    sprintf(response_buffer, "Content-Type: %s\r\n", content_type);
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    sprintf(response_buffer, "\r\n");
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    int read_length = fread(response_buffer, 1, RESPONSE_BUFFER_SIZE, file_pointer);
    while (read_length)
    {
        send(client->socket, response_buffer, read_length, 0);
        read_length = fread(response_buffer, 1, RESPONSE_BUFFER_SIZE, file_pointer);
    }

    fclose(file_pointer);
    drop_client(client);
}
// csv處理函數
/*
 * Return value:
 *   1  = data is different and has been saved
 *   0  = data is the same, nothing was saved
 *  -1  = file or time processing failed
 */
int save_weather_to_csv(int temperature, int humidity)
{
    FILE *file_pointer = fopen("weather.csv", "a+");

    if (!file_pointer)
    {
        perror("Cannot open weather.csv");
        return -1;
    }

    /*
     * a+ allows reading and appending.
     * Move to the beginning first so that we can find the latest record.
     */
    rewind(file_pointer);

    char line[256];

    int has_previous_record = 0;
    int previous_temperature = 0;
    int previous_humidity = 0;

    /*
     * CSV format:
     * datetime,temperature,humidity
     *
     * Example:
     * 2026-06-11 10:30:20,26,57
     */
    while (fgets(line, sizeof(line), file_pointer))
    {
        int parsed_temperature = 0;
        int parsed_humidity = 0;

        /*
         * Skip everything before the first comma,
         * then read temperature and humidity.
         */
        int parsed = sscanf(
            line,
            "%*[^,],%d,%d",
            &parsed_temperature,
            &parsed_humidity
        );

        if (parsed == 2)
        {
            previous_temperature = parsed_temperature;
            previous_humidity = parsed_humidity;
            has_previous_record = 1;
        }
    }

    if (
        has_previous_record &&
        previous_temperature == temperature &&
        previous_humidity == humidity
    )
    {
        printf(
            "same: temperature=%d, humidity=%d\n",
            temperature,
            humidity
        );

        fclose(file_pointer);
        return 0;
    }

    printf(
        "different! temperature=%d, humidity=%d\n",
        temperature,
        humidity
    );

    time_t current_time = time(NULL);

    if (current_time == (time_t)-1)
    {
        fprintf(stderr, "Cannot obtain current time.\n");
        fclose(file_pointer);
        return -1;
    }

    struct tm *local_time = localtime(&current_time);

    if (!local_time)
    {
        fprintf(stderr, "Cannot convert current time.\n");
        fclose(file_pointer);
        return -1;
    }

    char datetime[32];

    if (
        strftime(
            datetime,
            sizeof(datetime),
            "%Y-%m-%d %H:%M:%S",
            local_time
        ) == 0
    )
    {
        fprintf(stderr, "Cannot format current time.\n");
        fclose(file_pointer);
        return -1;
    }

    /*
     * Explicitly move to the end before appending.
     * With a+ writes are placed at the end anyway, but this makes
     * the intention clear after the preceding read operations.
     */
    fseek(file_pointer, 0, SEEK_END);

    if (
        fprintf(
            file_pointer,
            "%s,%d,%d\n",
            datetime,
            temperature,
            humidity
        ) < 0
    )
    {
        perror("Cannot write weather data");
        fclose(file_pointer);
        return -1;
    }

    fclose(file_pointer);
    return 1;
}

// 額外在http-server的基礎上加入這個處理新的input的part
void handle_weather_data(struct client_info *client, char *body)
{
    int temperature = 0;
    int humidity = 0;

    int parsed = sscanf(
        body,
        "%d,%d",
        &temperature,
        &humidity
    );

    if (parsed != 2)
    {
        send_status_code(client, 400, 0, 0);
        return;
    }

    printf("Temperature: %d\n", temperature);
    printf("Humidity: %d\n", humidity);

    /* 目前先回傳成功，之後再加 CSV。 */
    send_status_code(client, 200, 0, "Weather data received");
    save_weather_to_csv(temperature, humidity);
}


int main(int argc, char** argv)
{

#if defined(_WIN32)
    WSADATA socket_data;
    if (WSAStartup(MAKEWORD(2, 2), &socket_data)) {
        fprintf(stderr, "Failed to initialize.\n");
        return 1;
    }
#endif

    SOCKET server_socket = create_socket(0, "50000");

    while (1)
    {
        fd_set read_ready;
        read_ready = wait_on_clients(server_socket);
        if (FD_ISSET(server_socket, &read_ready))
        {
            struct client_info *client = get_client(-1);
            client->socket = accept(
                server_socket,
                (struct sockaddr*) &(client->address),
                &(client->address_length)
            );

            if (!ISVALIDSOCKET(client->socket))
            {
                fprintf(stderr, "accept() failed. (%d)\n", GETSOCKETERRNO());
                return 1;
            }

            printf("New Connection From %s. \n", get_client_address(client));
        }

        struct client_info *client = client_list;
        while (client)
        {
            struct client_info *next_client = client->next;

            if (FD_ISSET(client->socket, &read_ready))
            {
                if (MAX_REQUEST_SIZE == client->received)
                {
                    send_status_code(client, 400, 0, 0);
                    client = next_client;
                    continue;
                }

                /* Because we can't expect client to send all data at once.
                 * It is very likely that the client will slowly send the data
                 * Into the receiving buffer. Initially, the received = 0, 
                 * As more data arrives, it needs to move to the back to put more data.
                 */
                int received_bytes = recv(
                    client->socket,
                    client->request + client->received,
                    MAX_REQUEST_SIZE - client->received,
                    0
                );

                if (received_bytes < 1)
                {
                    printf("Unexpected disconnect from %s.\n", get_client_address(client));
                    drop_client(client);
                }
                else
                {
                    client->received += received_bytes;
                    client->request[client->received] = 0;

                    char *query_pointer = strstr(client->request, "\r\n\r\n");
                    if (query_pointer) {
                        if (strncmp("POST /weather ", client->request, strlen("POST /weather ") ) != 0) {
                            send_status_code(client, 400, 0, 0);
                        }
                        else {
                            char *body = query_pointer + 4;

                            char *content_length_pointer =
                                strstr(client->request, "Content-Length:");

                            if (!content_length_pointer) {
                                send_status_code(client, 400, 0, 0);
                            }
                            else {
                                int content_length = atoi(content_length_pointer + strlen("Content-Length:"));

                                int body_received = client->received - (int)(body - client->request);

                                // 這邊蠻重要的，因為沒收滿資料就下去跑會出現400 bad request。
                                if (body_received >= content_length)
                                {
                                    body[content_length] = '\0';
                                    handle_weather_data(client, body);
                                }
                            }
                        }
                    }
                }
            }
            client = next_client;
        }
    }

    printf("\nClosing socket...\n");
    CLOSESOCKET(server_socket);

#if defined(_WIN32)
    WSACleanup();
#endif

    printf("Finished.\n");
    return 0;
}