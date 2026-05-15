#include "sock_compat.h"
#include <time.h>

#define TIMEOUT 5.0
#define RESPONSE_SIZE 32768

void parse_url(
    char *url,
    char **hostname,
    char **port,
    char **path
)
{
    /* Implement Parse URL here. */
    char *read_pointer;
    // 直接jump 到http的url
    read_pointer = strstr(url, "://");
    
    char *protocol = 0;
    if (read_pointer) {
        // protocol = http
        protocol = url;
        // protocol 指著http這個字。
        // 本來指著 :// 的: -> 設定這個是0，然後+3就可以抓到hostname。
        *read_pointer = 0;
        read_pointer += 3;
    }
    // 如果當前沒有protocol
    else {
        read_pointer = url;
    }
    if (protocol) {
        // 一樣的話return 0 --> 不是0代表有問題
        if(strcmp(protocol, "http")) {
            fprintf(stderr, "Unknown Protocol\n");
            exit(1);
        }   
    }
    // 剛剛 +=3 所以現在hostname指著read_pointer
    *hostname = read_pointer;

    while (
        *read_pointer &&
        *read_pointer != ':' &&
        *read_pointer != '/' &&
        *read_pointer != '#'
    ) {
        // 一路跳到port前面
        read_pointer++;
    }
    // 預設80
    *port = "80";
    // 使用者可能會輸入怪怪的port
    if (*read_pointer == ':') {
        *read_pointer++ = 0;
        *port = read_pointer;
    }
    while(
        *read_pointer &&
        *read_pointer != '/' &&
        *read_pointer != '#'
    ) {
        // 一路跳到path
        read_pointer++;
    }
    *path = read_pointer;
    if (*read_pointer == '/') {
        *path = read_pointer+1;
    }
    *read_pointer = 0;
    while (
        *read_pointer &&
        *read_pointer != '#'
    ) {
        // 直接跳過#
        ++read_pointer;
    }
    if (*read_pointer == '#') {
        *read_pointer = 0;
    }
    printf("Hostname: %s\n", *hostname);
    printf("Port: %s\n", *port);
    printf("Path: %s\n", *path);
}

void send_request(
    SOCKET server_socket,
    char *hostname,
    char *port,
    char *path
) {
    /* Build and send your header here.*/
    char buffer[2048];
    sprintf(buffer, "GET /%s HTTP/1.1\r\n", path);
    sprintf(buffer+strlen(buffer), "Host: %s:%s\r\n", hostname, port);
    sprintf(buffer+strlen(buffer), "Connection: close\r\n");
    sprintf(buffer+strlen(buffer), "User-Agent: Wayne web_client\r\n");
    // 因為header跟body要分開，所以需要額外打\r\n
    sprintf(buffer+strlen(buffer), "\r\n");

    send(server_socket, buffer, strlen(buffer), 0);
    printf("Send Headers:\n%s", buffer);
}

SOCKET connect_to_host(char *hostname, char *port)
{
    printf("Configuring Remote Address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo *server_address;

    int getaddr_result = getaddrinfo(
        hostname, port, &hints, &server_address
    );

    if (getaddr_result)
    {
        fprintf(stderr, "getaddrinfo() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Remote Address is: ");
    char server_address_literal[100];
    char server_port_literal[100];
    int getname_result = getnameinfo(
        server_address->ai_addr,
        server_address->ai_addrlen,
        server_address_literal,
        sizeof(server_address_literal),
        server_port_literal,
        sizeof(server_port_literal),
        NI_NUMERICHOST | NI_NUMERICSERV
    );

    printf("%s %s\n", server_address_literal, server_port_literal);

    printf("Creating socket...\n");
    SOCKET server_socket;
    server_socket = socket(
        server_address->ai_family,
        server_address->ai_socktype,
        server_address->ai_protocol
    );

    if (!ISVALIDSOCKET(server_socket)) 
    {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Connecting...\n");
    int connect_result = connect(
        server_socket,
        server_address->ai_addr,
        server_address->ai_addrlen
    );

    if (connect_result)
    {
        fprintf(stderr, "connect() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }
    freeaddrinfo(server_address);

    printf("Server Connected.\n\n");
    return server_socket;
}

int main(int argc, char *argv[])
{
#if defined(_WIN32)
    WSADATA socket_data;
    if (WSAStartup(MAKEWORD(2, 2), &socket_data))
    {
        fprintf(stderr, "Failed to initialize.\n");
        return 1;
    }

#endif

    if (argc < 2)
    {
        fprintf(stderr, "Usage: web_client_get URL\n");
        return 1;
    }

    char *url = argv[1];
    char *hostname;
    char *port;
    char *path;

    /* Start Buildling Response Prerequisites */
    parse_url(url, &hostname, &port, &path);
    SOCKET server_socket = connect_to_host(hostname, port);
    send_request(server_socket, hostname, port, path);

    const clock_t start_time = clock();

    char response[RESPONSE_SIZE+1];
    char *response_pointer = response;
    char *query_pointer;
    char *end_of_response = response + RESPONSE_SIZE;
    char *content_body = 0;

    enum {length, chunked, connection};
    int encoding = 0;
    int remaining = 0;
    /*0508 fin here*/
    
    while (1)
    {
        if ((clock() - start_time) / CLOCKS_PER_SEC > TIMEOUT)
        {
            fprintf(stderr, "Timeout after %.2f seconds\n", TIMEOUT);
            return 1;
        }

        if (response_pointer == end_of_response)
        {
            fprintf(stderr, "Out of buffer space\n");
            return 1;
        }

        fd_set read_ready;
        FD_ZERO(&read_ready);
        FD_SET(server_socket, &read_ready);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;

        int select_result = select(
            server_socket + 1,
            &read_ready,
            0,
            0,
            &timeout
        );

        if (select_result < 0)
        {
            fprintf(stderr, "select() failed (%d)\n", GETSOCKETERRNO());
            return 1;
        }

        if (FD_ISSET(server_socket, &read_ready))
        {
            int bytes_received = recv(
                server_socket,
                response_pointer,
                end_of_response - response_pointer,
                0
            );

            if (bytes_received < 1)
            {
                if (encoding == connection && content_body)
                {
                    printf("%.*s", (int)(end_of_response - content_body), content_body);
                }

                printf("\nConnection closed by server.\n");
                break;
            }

            printf("Received (%d bytes): '%.*s'", bytes_received, bytes_received, response_pointer);

            /* Start Handling Response Here. */
            response_pointer += bytes_received;
            // 透過設定他為0去分出header跟data的區別
            *response_pointer = 0;
            // ? 看不懂
            if(!content_body && (content_body = strstr(response, "\r\n\r\n"))) {
                *content_body = 0;
                content_body += 4;
                
                printf("Received Headers: \n%s\n", response);
                query_pointer = strstr(response, "\nContent-Length: ");
                // 如果query_pointer裡面有寫Content-Length -- 不用猜
                if (query_pointer) {
                    encoding = length;
                    // 本來會指著Content-Length 後面的空白，所以要往後移動一格抓數字本身。
                    query_pointer = strchr(query_pointer, ' ');
                    query_pointer += 1;
                    remaining = strtol(query_pointer, 0, 10);
                }
                else {
                    query_pointer = strstr(response, "\nTransfer-Encoding: chunked");
                    if (query_pointer) {
                        encoding = chunked;
                        // 因為我們暫時沒有這個資訊，就先設定他為0。
                        remaining = 0;
                    }
                    else {
                        encoding = connection;
                    }
                }
                printf("\nReceived Body:\n");
            }
            if (content_body) {
                if (encoding == length) {
                    if (response_pointer-content_body >= remaining){
                        printf("%.*s", remaining, content_body);
                        break;
                    }
                }
                else if (encoding == chunked) {
                    do{
                        if (remaining == 0) {
                            if ((query_pointer = strstr(content_body, "\r\n"))) {
                                // 不知道在幹嘛
                                remaining = strtol(content_body, 0, 16);
                                if (!remaining) {
                                    // 不知道在幹嘛
                                    goto finish;
                                }
                                content_body = query_pointer+2;
                            }
                            else {
                                break;
                            }
                        }
                        if (remaining &&
                            response_pointer-content_body >= remaining
                        ){
                            printf("%.*s", remaining, content_body);
                            content_body += remaining+2;
                            remaining = 0;
                        }
                    } while(!remaining);
                    /* ex.
                    4
                    NYCU
                    4
                    IAIS
                    0
                    */
                }
            }
        }
    }

finish:
    printf("\nClosing socket...\n");
    CLOSESOCKET(server_socket);

#if defined(_WIN32)
    WSACleanup();
#endif

    printf("Finished.\n");
    return 0;
}