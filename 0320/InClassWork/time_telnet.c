#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else 
    #include <sys/types.h>  // for useful data type
    #include <sys/socket.h>
    #include <netinet/in.h> // Internet Protocol family
    #include <arpa/inet.h>  // for internet operation
    #include <netdb.h>      // for network database operation
    #include <unistd.h>     // for POSIX
    #include <errno.h>
#endif
#if defined(_WIN32)
    #define ISVALIDSOCKET(s) ((s) != INVALID_SOCKET)
    #define CLOSESOCKET(s) closesocket(s)
    #define GETSOCKETERRNO() (WSAGetLastError())
#else
    // the (s) >= 0 -> if-else -> true/false!
    #define ISVALIDSOCKET(s) ((s) >= 0)
    #define CLOSESOCKET(s) close(s)
    #define GETSOCKETERRNO() (errno)
    #define SOCKET int
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> 

int main() {
    // only for windows socket
    #if defined(_WIN32)
    WSADATA socket_data;
    // MAKEWORD(2, 2) is to specifiy the version of WSA
    if(WSAStartup(MAKEWORD(2, 2), &socket_data)) {
        fprintf("stderr", "Failed to init WSA\n");
        return 1;
    }
    #endif
    printf("Setting up local address...\n");
    struct addrinfo hints;
    // set a bunch of zeros in it to init!
    memset(&hints, 0, sizeof(hints));

    // AF_INET: means IPv4
    hints.ai_family = AF_INET;
    // SOCK_STREAM: means TCP
    hints.ai_socktype = SOCK_STREAM;
    // waiting people to reach me -- PASSIVE
    hints.ai_flags = AI_PASSIVE;
    // though calling SOCK_STREAM -- still can declare protocol to be TCP
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *bind_addresses;
    // 0: means not specify, ask system to select a suitable one for me.
    // "8023" for port
    // hints: tell system which kind of address we want to get.
    // eventually, make human readable ip+port -> machine readable binary -- socket!
    getaddrinfo(0, "8080", &hints, &bind_addresses);

    printf("Creating socket...\n");
    SOCKET socket_listen; // SOCKET in unix just int...
    // init, what IPv?, what socktype? (TCP? UDP?)...
    socket_listen = socket(
        bind_addresses->ai_family,
        bind_addresses->ai_socktype,
        bind_addresses->ai_protocol
    );
    if(!ISVALIDSOCKET(socket_listen)) {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }
    // really bind the socket to the metal
    int bind_result = bind(
        socket_listen,
        bind_addresses->ai_addr,
        bind_addresses->ai_addrlen
    );
    if (bind_result != 0) {
        fprintf(stderr, "bind() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }
    freeaddrinfo(bind_addresses);
    
    // 10 means  
    printf("Listening...\n");
    // 10: means the maxi num that haven't be accept can be queue;
    int listen_result = listen(
        socket_listen,
        10
    );
    // any number means error
    if(listen_result) {
        fprintf(stderr, "listen() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }

    printf("Waiting for connection...\n");
    struct sockaddr_storage client_address;
    socklen_t client_len = sizeof(client_address);

    SOCKET socket_client = accept(
        socket_listen,
        (struct sockaddr *) &client_address,
        &client_len
    );
    if(!ISVALIDSOCKET(socket_client)) {
        fprintf(stderr, "accept() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }
    
    printf("Client is connected.\n");
    char address_buffer[100];
    // reverse operation of getaddrinfo
    // transform client_addr(binary) -> human readable stuff, catch with address_buffer!
    // 0, 0: 
    getnameinfo(
        (struct sockaddr *) &client_address,
        client_len,
        address_buffer,
        sizeof(address_buffer),
        0,
        0,
        NI_NUMERICHOST // I just want ip, not name.
    );
    printf("%s\n", address_buffer);
    
    // 1. HTTP requires reading the request first
    // special for http!, since even you don't want to do anything.
    char request[1024];
    int bytes_received = recv(socket_client, request, 1024, 0);
    if (bytes_received > 0) {
        printf("Received HTTP Request:\n%.*s\n", bytes_received, request);
    }

    int bytes_sent;
    time_t timer;
    time(&timer);
    char *time_msg = ctime(&timer); 

    // 2. Construct HTTP Response (Header + Body)
    char response[1024];
    sprintf(response, 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s", 
        (unsigned long)strlen(time_msg), 
        time_msg
    );

    bytes_sent = send(
        socket_client,
        response,
        strlen(response),
        0
    );
    
    printf("Closing connection...\n");
    CLOSESOCKET(socket_client);
    printf("Closing listening socket...\n");
    CLOSESOCKET(socket_listen);
    #if defined(_WIN32)
    WSACleanup();
    #endif
    printf("all Done.\n");
    return 0;
}
    