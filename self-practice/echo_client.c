#include <stdio.h>
#include "sock_compact.h"

int main(int argc, char** argv) {
    char* remote_host = argv[1]; 
    char* remote_port = argv[2];
    char* msg = argv[3];
    struct addrinfo hints;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    // 這裡getaddrinfo是get server的，不是自己的，因為connect會自動隨機分配一個host:port給你，不需要特別getaddrinfo
    struct addrinfo *connect_address;
    getaddrinfo(remote_host, remote_port, &hints, &connect_address);
    SOCKET socket_connect = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);
    int connect_result = connect(socket_connect, connect_address->ai_addr, connect_address->ai_addrlen);
    // 送訊息
    char sentMsg[1024];
    char buffer[1024];
    snprintf(sentMsg, sizeof(sentMsg), "%s", msg);
    // send必須是strlen是因為裡面有多少送多少就好。
    send(socket_connect, sentMsg, strlen(sentMsg), 0);
    // recv必須是sizeof是因為他現在裡面沒有任何strlen在裡面，你用strlen沒有意義
    recv(socket_connect, buffer, sizeof(buffer), 0);
    printf("%s\n", buffer);
}