#include "sock_compat.h"

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

    /* Fill in the logic of get_content_type */
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
    /* Fill in the client_info structure. */
    struct sockaddr_storage address;
    socklen_t address_length;
    SOCKET socket;
    char request[MAX_REQUEST_SIZE+1];
    int received;
    struct client_info *next;
};

static struct client_info *client_list = 0;

struct client_info *get_client (SOCKET query_socket)
{
    /* Fill in the logic of get_client */
    struct client_info *client = client_list;
    // 看他有沒有東西
    while(client) {
        // 如果找到對應的query_socket，就break
        if (client->socket == query_socket) {
            break;
        }
        // 如果沒找到目標就往後看
        else {
            client = client->next;
        }
    }
    // 如果找不到，上面的else會把它指到NULL
    if (client) {
        return client;
    }
    // 如果找不到，剛剛的else會把它指到NULL --> 他可能是new client
    struct client_info *new_client = (struct client_info*) calloc(1, sizeof(struct client_info));
    if (!new_client) {
        fprintf(stderr, "Out of memory, cannot create new client");
        exit(1);
    }
    new_client->address_length = sizeof(new_client->address);
    // 把new_client的next接到當前的client_list頭
    new_client->next = client_list;
    // move client_list頭
    client_list = new_client;
    return new_client;
}

void drop_client (struct client_info *to_drop_client)
{
    /* Fill in the logic of drop_client */
    CLOSESOCKET(to_drop_client->socket);
    struct client_info **client_list_pointer = &client_list;
    while (*client_list_pointer) {
        if (*client_list_pointer) {
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
    /* Fill in the logic of get_client_address */
    char address_liternal[128];
    getnameinfo(
        (struct sockaddr*) &client->address,
        client->address_length,
        address_liternal,
        sizeof(address_liternal),
        0,
        0,
        NI_NUMERICHOST
    );
    return address_liternal;
}

fd_set wait_on_clients(SOCKET server_socket)
{
    /* Fill in the logic of wait_on_clients */
    fd_set read_ready;
    // ?
    FD_ZERO(&read_ready);
    // ?
    FD_SET(server_socket, &read_ready);
    // ?
    SOCKET max_socket = server_socket;
    
    struct client_info *client = client_list;

    while (client) {
        // ?
        FD_SET(client->socket, &read_ready);
        if(client->socket > max_socket) {
            max_socket = client->socket;
        }
        client = client->next;
    }
    int select_result = select(
        max_socket+1,
        &read_ready,
        0,
        0,
        0
    );
    if (select_result < 0) {
        fprintf(stderr, "select() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }
    // ?
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
    /* Fill in the logic of send_status_code */
    char to_send_response;
    memset(to_send_response, 0, sizeof(to_send_client));
    switch (status_code) {
        case 400:
            sprintf(
                to_send_response+strlen(to_send_response),
                "HTTP/1.1 %d Bad Request\r\n", status_code
            );
            sprintf(
                to_send_response+strlen(to_send_response),
                "Connection: close\r\n"
            );
            if (additional_content == 0) {
                sprintf(
                    to_send_response+strlen(to_send_response),
                    "Content-Length: %d\r\n", strlen("Bad Request")
                );
                sprintf(
                    to_send_response+strlen(to_send_response),
                    "\r\n"
                );
            }
            else {
                sprintf(
                    to_send_response+strlen(to_send_response),
                    "Content-Length: %d\r\n", strlen("additional_content")
                );
                sprintf(
                    to_send_response+strlen(to_send_response),
                    "\r\n"
                );
                sprintf(
                    to_send_response+strlen(to_send_response),
                    additional_content
                );
            }
    }
}

#define RESPONSE_BUFFER_SIZE 1024
void send_resource
(
    struct client_info *client,
    const char *path
)
{
    /* Fill in the logic of send_resource */
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

    SOCKET server_socket = create_socket(0, "8080");

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
            /* Fill in the logic of client handling. */
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