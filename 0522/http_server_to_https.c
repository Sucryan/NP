#include "sock_compat.h"
#include "openssl.h"
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
    // 因為每個client都是SSL
    SSL *secure_socket;
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
{   // 因為加入了SSL，所以需要先shutdown SSL
    SSL_shutdown(to_drop_client->secure_socket);
    CLOSESOCKET(to_drop_client->socket);
    SSL_free(to_drop_client->secure_socket);
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
        default:
            sprintf(to_send_response + strlen(to_send_response), "HTTP/1.1 500 Internal Server Error\r\n");
            sprintf(to_send_response + strlen(to_send_response), "Connection: close\r\n");
            sprintf(to_send_response + strlen(to_send_response), "Content-Length: %d\r\n", strlen("Server issued an unknown status code."));
            sprintf(to_send_response + strlen(to_send_response), "\r\n");
            sprintf(to_send_response + strlen(to_send_response), "Server issued an unknown status code.");
            fprintf(stderr, "Unknown status code (%d)\n", status_code);
            break;
    }
    SSL_write(
        to_send_client->secure_socket,
        to_send_response,
        strlen(to_send_response)
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
    SSL_write(client->secure_socket, response_buffer, strlen(response_buffer));

    sprintf(response_buffer, "Connection: close\r\n");
    SSL_write(client->secure_socket, response_buffer, strlen(response_buffer));

    sprintf(response_buffer, "Content-Length: %u\r\n", file_size);
    SSL_write(client->secure_socket, response_buffer, strlen(response_buffer));

    sprintf(response_buffer, "Content-Type: %s\r\n", content_type);
    SSL_write(client->secure_socket, response_buffer, strlen(response_buffer));

    sprintf(response_buffer, "\r\n");
    SSL_write(client->secure_socket, response_buffer, strlen(response_buffer));

    int read_length = fread(response_buffer, 1, RESPONSE_BUFFER_SIZE, file_pointer);
    while (read_length)
    {
        SSL_write(client->secure_socket, response_buffer, read_length);
        read_length = fread(response_buffer, 1, RESPONSE_BUFFER_SIZE, file_pointer);
    }

    fclose(file_pointer);
    drop_client(client);
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
    /* SSL init */
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    SSL_CTX *ssl_context = SSL_CTX_new(TLS_server_method());
    if(!ssl_context) {
        fprintf(stderr, "Cannot create SSL context.\n");
        return 1;
    }
    // 用int去看cert有沒有正確
    int load_cert_result = SSL_CTX_use_certificate_file(
        ssl_context,
        "certificate.pem",
        SSL_FILETYPE_PEM
    );

    int load_privkey_result = SSL_CTX_use_PrivateKey_file(
        ssl_context,
        "private_key.pem",
        SSL_FILETYPE_PEM
    );

    if(!load_cert_result || !load_privkey_result) {
        fprintf(stderr, "Cannot load keys.\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }


    SOCKET server_socket = create_socket(0, "8443");

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
            /* 加入client secure socket */
            client->secure_socket = SSL_new(ssl_context);
            if (!client->secure_socket) {
                fprintf(stderr, "Cannot create SSL session.\n");
                return 1;
            }
            // 為什麼會同時需要secure_socket跟client->socket?
            SSL_set_fd(client->secure_socket, client->socket);
            int SSL_accept_result = SSL_accept(client->secure_socket);
            if(SSL_accept_result != 1) {
                fprintf(stderr, "Client SSL failed.\n");
                // 這個到底在幹嘛？為什麼有一些有有一些沒有？
                ERR_print_errors_fp(stderr);
                drop_client(client);
                return 1;
            }
            else{
                printf("New Connection From %s. \n", get_client_address(client));
                printf("SSL Connection using %s. \n", SSL_get_cipher(client->secure_socket));
            }
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
                int received_bytes = SSL_read(
                    client->secure_socket,
                    client->request + client->received,
                    MAX_REQUEST_SIZE - client->received
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
                    if (query_pointer)
                    {
                        *query_pointer = 0;

                        if (strncmp("GET /", client->request, 5))
                        {
                            send_status_code(client, 400, 0, 0);
                            /* If you would like to implement more, you can change
                             * The logic here. In this case we only accept GET.
                             */
                        }
                        else
                        {
                            char *path = client->request + 4;
                            char *end_path = strstr(path, " ");
                            if (!end_path)
                            {
                                send_status_code(client, 400, 0, 0);
                            }
                            else
                            {
                                *end_path = 0;
                                send_resource(client, path);
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
    SSL_CTX_free(ssl_context);
    
#if defined(_WIN32)
    WSACleanup();
#endif

    printf("Finished.\n");
    return 0;
}