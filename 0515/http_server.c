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
    // 我們想抓的是整個路徑的最後一個.，例如"public/archive.v1.html, 我們想知道他是.html
    const char *last_dot = strrchr(path, '.');
    if (last_dot) {
        for(size_t i = 0; i < sizeof(extension)/sizeof(char*); i++) {
            if (strcmp(last_dot, extension[i]) == 0) {
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
        if (*client_list_pointer == to_drop_client) {
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
    static char address_liternal[128];
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
    const char *reason_phrase = 0;
    const char *default_content = 0;

    switch (status_code) {
        case 400:
            reason_phrase = "Bad Request";
            default_content = "Bad Request";
            break;
        case 404:
            reason_phrase = "Not Found";
            default_content = "Not Found";
            break;
        case 500:
            reason_phrase = "Internal Server Error";
            default_content = "Internal Server Error";
            break;
        default:
            fprintf(stderr, "Unknown status code (%d)\n", status_code);
            status_code = 500;
            reason_phrase = "Internal Server Error";
            default_content = "Server issued an unknown status code.";
            break;
    }

    const char *content = additional_content ? additional_content : default_content;
    char to_send_response[1024];
    memset(to_send_response, 0, sizeof(to_send_response));

    sprintf(
        to_send_response + strlen(to_send_response),
        "HTTP/1.1 %d %s\r\n",
        status_code,
        reason_phrase
    );
    sprintf(to_send_response + strlen(to_send_response), "Connection: close\r\n");
    if (additional_header) {
        sprintf(to_send_response + strlen(to_send_response), "%s", additional_header);
    }
    sprintf(
        to_send_response + strlen(to_send_response),
        "Content-Length: %zu\r\n",
        strlen(content)
    );
    sprintf(to_send_response + strlen(to_send_response), "\r\n");
    sprintf(to_send_response + strlen(to_send_response), "%s", content);

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
    /* Fill in the logic of send_resource */
    // ?
    /*
    www.example.com/index.html, www.example.com/index.htm --> index.html, index.htm...
    */
    printf(
        "sending_resource %s %s \n",
        get_client_address(client),
        path
    );
    if (strcmp(path, "/") == 0) {
        path = "/index.html";
    }
    // 如果使用者傳很長的東西過來，做簡單的自我保護 --> 送400給他。
    if(strlen(path) > 128) {
        send_status_code(client, 400, 0, 0);
        return;
    }
    if (strstr(path, "..")) {
        send_status_code(client, 404, 0, 0);
        return;
    }
    // 這個是for backward capability, 用超過這個可能會導致老系統爆開。
    char full_path[256];
    // 會讀public這個資料夾。
    sprintf(full_path, "public%s", path);
    // tricky part -- main diff of WIN and LINUX -- 
    /* 
        C:\USER\Wayne\file.txt
        /home/wayne/file.txt
    */
#if defined(_WIN32)
    char *full_path_pointer = full_path;
    while(*full_path_pointer){
        if(*full_path_pointer == '/') {
            *full_path_pointer == '\\';
        }
        full_path_pointer++;
    }
#endif
    FILE *file_pointer = fopen(full_path, "rb");
    if(!file_pointer) {
        send_status_code(client, 404, 0, 0);
        return;
    }
    // ?
    fseek(file_pointer, 0L, SEEK_END);
    // ?
    size_t file_size = ftell(file_pointer);
    // ?
    rewind(file_pointer);

    const char* content_type = get_content_type(full_path);

    char response_buffer[RESPONSE_BUFFER_SIZE];

    // Header Part
    sprintf(response_buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    sprintf(response_buffer, "Connection: close\r\n");
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    sprintf(response_buffer, "Content-Length: %zu\r\n", file_size);
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    sprintf(response_buffer, "Content-Type: %s\r\n", content_type);
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    sprintf(response_buffer, "\r\n");
    send(client->socket, response_buffer, strlen(response_buffer), 0);

    // Content Part
    int read_length = fread(
        response_buffer,
        1,
        RESPONSE_BUFFER_SIZE,
        file_pointer
    );
    // ?
    while (read_length) {
        send(
            client->socket,
            response_buffer,
            read_length,
            0
        );
        read_length = fread(
            response_buffer,
            1,
            RESPONSE_BUFFER_SIZE,
            file_pointer
        );
    }
    fclose(file_pointer);
    drop_client(client);
}

int main(int argc, char** argv)
{
    // 如果比較嚴格的編譯，他會跳warning寫unused parameter -- 所以把它 cast 成 void 相當於讓編譯器知道我是故意沒有要用他們的。
    (void)argc;
    (void)argv;

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
        // fd_set 會建立一張有可讀的事件的清單（可能是有新連線或者 client_socket 傳 request 來了）
        fd_set read_ready;
        // 然後wait_on_client會負責等至少有一個新事件，就 return 給 read_ready
        read_ready = wait_on_clients(server_socket);
        // FD_ISSET == True代表這一輪server_socket有事情要處理 -- 代表有新的TCP connection排在accept queue裡面，可以給你accept了。
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
                    if (query_pointer)
                    {
                        *query_pointer = 0;

                        if (strncmp("GET /", client->request, 5))
                        {
                            send_status_code(client, 400, 0, 0);
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

#if defined(_WIN32)
    WSACleanup();
#endif

    printf("Finished.\n");
    return 0;
}
