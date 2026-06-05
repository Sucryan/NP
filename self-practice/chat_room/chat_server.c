#include <stdio.h>
#include "sock_compact.h"

int main(int argc, char** argv) {
    // 寫tcp版
    struct addrinfo hints;
    hints.ai_family = AF_INET; // ipv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo *bind_address;
    // 將server ip+port轉換成binary
    getaddrinfo(NULL, "8080", &hints, &bind_address);
    // 只是create 一個 fd
    SOCKET socket_listen = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);
    // 在bind的時候才實際把binary address刻進去，在client端則要等到connect
    int bind_result = bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen);
    int listen_result = listen(socket_listen, 10);
    freeaddrinfo(&bind_address);
    // 先建立一個叫master的fd_set，用來記錄到底有誰是成功跟我們建立連線的。
    // 因為select如果發現那些我們在監控的fd沒反應，就會把它重置成0，等到有人突然跳說要連線才醒來往後走。
    // 所以每次read_ready都要額外複製master的狀態。
    fd_set master;
    FD_ZERO(&master);
    FD_SET(socket_listen, &master);
    char* user_name[1024];
    // 紀錄當前最大值 -- for select
    int max_socket = socket_listen;
    while(1){
        fd_set read_ready;
        read_ready = master;
        // 卡在這邊，監控read_ready誰有反應。
        select(max_socket+1, &read_ready, NULL, NULL, NULL);
        // 跳出來代表select讀到有東西了。
        for (int i = 0; i < max_socket+1; i++) {
            // 代表有個新連線
            if (i == socket_listen) {
                struct sockaddr_storage client_address;
                socklen_t client_len = sizeof(client_address);
                SOCKET new_socket = accept(socket_listen, (struct sockaddr *) &client_address, client_len);
                if (new_socket > max_socket) {
                    max_socket = new_socket;
                }
                FD_SET(new_socket, &master);
                // 送打招呼訊息過去。
                char hello_msg[128];
                sprintf(hello_msg, "Welcome to join Sucryan's Chat Room!\nPlease tell me your name\n");
                send(new_socket, hello_msg, strlen(hello_msg), 0);
                // 接收使用者名稱。
                char buffer[128];
                recv(new_socket, buffer, sizeof(buffer), 0);
                user_name[new_socket] = buffer;
            }
            // 如果 select 有反應但不是 listen socket
            else {
                char buffer[512];
                recv(i, buffer, sizeof(buffer), 0);
                char broadcast_msg[1024];
                sprintf(broadcast_msg, "%s said: %s\n", user_name[i], buffer);
                // broadcast這個訊息給所有其他人
                for (int j = 0; j < max_socket+1; j++) {
                    // 如果是自己或者listening socket -- 不要送broadcast過去
                    if (j == socket_listen || j == i) {
                        continue;
                    }
                    // 如果在master顯示有連線
                    if (FD_ISSET(j, &master)) {
                        send(j, broadcast_msg, strlen(broadcast_msg), 0);
                    }
                }
            }
        }
    }
    CLOSESOCKET(socket_listen);
}