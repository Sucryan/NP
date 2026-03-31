#include "sock_compact.h"

#if defined(_WIN32)
    // windows(DOS): if there is a special key input(ex. ctrl+c, enter, escape, shift...)
    #include <conio.h>
#endif

int main(int argc, char* argv[]){
    #if defined(_WIN32)
    WSADATA sock_data;
    if(WSAStartup(MAKEWORD(2, 2), &sock_data)) {
        fprintf(stderr, "Failed to init WinSock2.\n");
        return 1;
    }
    #endif

    char* dest_host = argv[1];
    char* dest_port = argv[2];

    if(argc < 3) {
        fprintf(stderr, "usage: tcp_client [hostname] [port].\n");
        return 1;
    }
    // set up socket hints
    printf("Configuring remote address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    // change human readable ip+port, and pass hints into it -> make it a machine readable socket!
    struct addrinfo *dest_address;
    int getaddr_result = getaddrinfo(dest_host, dest_port, &hints, &dest_address);
    if(getaddr_result) {
        fprintf(stderr, "getaddrinfo() failed \n");
        return 1;
    }
    printf("Remote Address is: ");
    char dest_addr_buf[10];
    char dest_port_buf[10];
    // change the machine readable socket(ip+port) -> back into human readable
    // which in here can let us verify if our setting is really correct!
    getnameinfo(
        dest_address->ai_addr, 
        dest_address->ai_addrlen, 
        dest_addr_buf, 
        sizeof(dest_addr_buf), 
        dest_port_buf, 
        sizeof(dest_port_buf), 
        NI_NUMERICHOST
    );

    printf("%s %s\n", dest_addr_buf, dest_port_buf);

    printf("Creating socket...\n");
    SOCKET socket_to_dest;
    socket_to_dest = socket(dest_address->ai_family, dest_address->ai_socktype, dest_address->ai_protocol);

    if(!ISVALIDSOCKET(socket_to_dest)) {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }

    printf("Connecting...\n");
    int connect_result = connect(
        socket_to_dest, 
        dest_address->ai_addr,
        dest_address->ai_addrlen
    );

    if(connect_result) {
        fprintf(stderr, "connect failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }

    freeaddrinfo(dest_address);

    while(1) {
        // set an array to store select stuff
        fd_set read_ready;
        FD_ZERO(&read_ready);
        FD_SET(socket_to_dest, &read_ready);
        // which means this is an unix stuff!
        #if !defined(_WIN32) 
        FD_SET(fileno(stdin), &read_ready);
        #endif

        struct timeval select_timeout;
        select_timeout.tv_sec = 0;
        select_timeout.tv_usec = 100000;
        // first 0: check if there exist another space for use to input
        // second 0: check if there is any error of the fd?
        int select_result = select(socket_to_dest+1, &read_ready, 0, 0, &select_timeout);
        if(select_result < 0) {
            fprintf(stderr, "select() failed. (%d)\n", GETSOCKETERRNO());
            return 1;
        }
        
        if(FD_ISSET(socket_to_dest, &read_ready)) {
            char received[4096];
            int bytes_received = recv(
                socket_to_dest, 
                &received, 
                4096,
                0
            );
            if(bytes_received < 1) {
                printf("Connection closed by destination.\n");
                break;
            }
            printf("Received (%d) bytes %s", bytes_received, received);
        }
        // trick part -- we have to check the keyboard event right now.(since we might press enter? ctrl+c? ctrl+d?)
        // dileama, since windows and linux handle keyboard differently
        // since only if part of it is different, so we just grap the if with #if, #else #endif
        #if defined(_WIN32)
        if(_kbit()) {

        #else 
        if(FD_ISSET((fileno(stdin)), &read_ready)) {
        #endif

            char to_send[4096];
            if(!fgets(to_send, 4096, stdin)) {
                break;
            }
            printf("Sending: %s", to_send);
            int byte_send = send(
                socket_to_dest,
                to_send,
                strlen(to_send),
                0
            );
        } 
    }
    printf("Closing socket...\n");
    CLOSESOCKET(socket_to_dest);

    #if defined(_WIN32)
    WSACleanup();
    #endif

    printf("Finished.\n");
    return 0;
}
