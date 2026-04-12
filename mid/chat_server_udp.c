#include "sock_compact.h"
#include <time.h>
// 因為UDP不會開一堆fd出來，他從頭到尾只有一個socket，所以被迫只能額外創一個 socket <--> user_name的struct，然後創一整條array去存。
struct UDPNode {
    int is_active;
    char ip_port_str[64]; // 可以用strcmp快速比。
    char name[256];

    // 存真正的地址用的
    struct sockaddr_storage address;
    socklen_t addr_len;
};

// 跟TCP一樣開1024個好了。
struct UDPNode clients[1024];

// 流程跟client不一樣。
// client: getaddrinfo -> create socket -> connect.
// server: getaddrinfo -> create socket -> bind -> listen -> accept.
int main(int argc, char* argv[]) {
    #if defined (_WIN32)
    WSADATA sock_data;
    if (WSAStartup(MAKEWORD(2, 2), &sock_data))
    {
        fprintf(stderr, "Failed to initialize WinSock2\n");
        return 1;
    }
    #endif
    for(int i = 0; i < 1024; i++) {
        // 先初始化他們全部都是0
        clients[i].is_active = 0;
    }
    // 其他大部分先複製貼上。
    // 跟client一樣設定hint
    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;   // 改成UDP
    hints.ai_flags = AI_PASSIVE;      // 告訴hints說這個socket等等是要負責bind+listen的。
    hints.ai_protocol = IPPROTO_UDP;  // 改成UDP
    
    struct addrinfo *bind_address;
    // 透過前面設定AI_PASSIVE，讓設定0代表預設 aka 0.0.0.0。
    // 稍微跟TCP錯開，後續或許可以寫同時activate，如果TCP掛了就走UDP之類的酷酷功能。
    getaddrinfo(0, "8024", &hints, &bind_address);

    // 建立Socket
    printf("Creating Socket...\n");
    SOCKET socket_listen;
    socket_listen = socket(
        bind_address->ai_family,
        bind_address->ai_socktype,
        bind_address->ai_protocol
    );
    if(!ISVALIDSOCKET(socket_listen)) {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }
    // bind相當於固定這個socket
    // server會需要bind，因為他 ip+port 這個 socket 必須是固定的，但client去連server反正port根本沒差，隨便找個就行。
    printf("Binding Socket to local address...\n");
    int bind_result = bind(
        socket_listen,
        bind_address->ai_addr,
        bind_address->ai_addrlen
    );
    if(bind_result) {
        fprintf(stderr, "bind() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }
    // 把佔用的東西free掉
    freeaddrinfo(bind_address);
    // 也不用listen
    // 標註這個東西是負責listen的。
    // printf("Listening...\n");
    // 10代表buffer size，在accept處理前會先存在那邊，超過他才會丟掉。
    // 在buffer裡面我們會先做three-way handshake。
    // int listen_result = listen(
    //     socket_listen,
    //     10
    // );
    // 用fd_set，主要就是等等可以看哪些人是亮著的。
    // 需要用兩個fd_set是因為一個是看誰是連著的，一個是看有沒有新的訊號進來。
    // 而看新的訊號那個因為會被一直修改，所以必須要用額外的所謂active_socket去存著所有連著的。
    fd_set active_sockets;
    FD_ZERO(&active_sockets);
    FD_SET(socket_listen, &active_sockets); // FD_SET先設定socket_listen是1，其他人進來也會被設定是1
    SOCKET max_socket = socket_listen;      // 當前的max是這個listen，後續有其他連線進來會變大。
    
    // 沒有那個names了，因為不能偷懶..
    // 因為我們需要存fd和name的對應，這樣才可以知道叫做什麼名字的人說了啥話。
    // 之所以設定1024是因為fd_set最大是1024（更大要用epoll）
    // char names[1024][256] = {0};

    printf("Waiting for connections...\n");
    // 因為UDP跟TCP最大的差別就在這邊，下面直接砍掉重練。
    while(1) {
        fd_set read_ready;
        read_ready = active_sockets;
        
        // 其實是可以不用select，因為他從頭到尾只會有一個socket_listen。
        int read_select_result = select(
            max_socket + 1,
            &read_ready,
            0,
            0,
            0
        );
        if (read_select_result < 0) {
            fprintf(stderr, "select() failed. (%d)\n", GETSOCKETERRNO());
            return 1;
        }
        if (FD_ISSET(socket_listen, &read_ready)) {
            struct sockaddr_storage client_address;
            socklen_t client_address_length = sizeof(client_address);

            char received[1024];
            int bytes_received = recvfrom(
                socket_listen,
                received,
                1024,
                0,
                (struct sockaddr *) &client_address,
                &client_address_length
            );

            if (bytes_received < 1) {
                fprintf(stderr, "connection closed. (%d)\n", GETSOCKETERRNO());
                return 1;
            }
            // 把最後面加上\0，跟tcp server一樣，避免後面髒
            received[bytes_received] = '\0';
            // 細節：主要是因為C沒有hashmap，等等就直接硬幹看整個1024的大小，
            // 去比對誰是傳這個訊息的人，跟誰是活著然後要負責被broadcast的人。
            // 先把ip+port的string做出來作為identifier
            char ip_str[NI_MAXHOST];
            char port_str[NI_MAXSERV];
            getnameinfo(
                (struct sockaddr *) &client_address,
                client_address_length,
                ip_str,
                sizeof(ip_str),
                port_str,
                sizeof(port_str),
                NI_NUMERICHOST
            );
            char cur_ip_port_str[64];
            snprintf(cur_ip_port_str, sizeof(cur_ip_port_str), "%s:%s", ip_str, port_str);
            // 如果UDPNode的clients中如果沒有找到對應的目標 --> 代表他是新客人，所以要挖空間給他。
            int empty_idx = -1;
            // 如果有找到那就先標著他，因為我回圈還要往後走去看有哪些人都是顯示is_active，這樣才能去做broadcast。
            int find_idx = -1; 
            if (bytes_received > 0) {
                // 先掃描一次看看狀況。
                for(int i = 0; i < 1024; i++) {
                    // 順手挖空間，以免後面有人要進來的時候不知道塞哪。
                    if(clients[i].is_active == 0 && empty_idx == -1) {
                        empty_idx = i;
                    }
                    // 有找到人 (必須先確認find_idx是不是找到了，找到了的話那就直接一直掃描empty_idx就好)
                    if(find_idx == -1 && clients[i].is_active == 1 && strcmp(clients[i].ip_port_str, cur_ip_port_str) == 0) {
                        find_idx = i;
                        // 如果有找到人，那回圈可以先結束了
                        if(empty_idx != -1) break;
                        else continue;
                    }
                }
                // 掃描完第一次了，開始思考他是第一次進來還是他這個訊息是要broadcast出去的。
                // 如果沒找到 aka 新客人
                if(find_idx == -1) {
                    // 有空位！
                    if(empty_idx != -1) {
                        // 把該塞的東西塞進去 UDPNode 裡面
                        clients[empty_idx].is_active = 1;
                        strncpy(clients[empty_idx].ip_port_str, cur_ip_port_str, 63);
                        strncpy(clients[empty_idx].name, received, 255);
                        // 為了後續要廣播所以也需要把他的address抄下來然後存起來。
                        clients[empty_idx].address = client_address;
                        clients[empty_idx].addr_len = client_address_length;

                        // 組一個greeting給他
                        char greeting_msg[1024];
                        snprintf(greeting_msg, sizeof(greeting_msg), "Hello %s, to send message, enter text followed by enter.\n", received);
                        sendto(socket_listen, greeting_msg, strlen(greeting_msg), 0, (struct sockaddr *)&client_address, client_address_length);
                        printf("New connection from %s\n", cur_ip_port_str);
                    }
                    else {
                        printf("Server is FULL :(\n");
                    }
                }
                // 如果find_idx 不是 -1 --> 老朋友，所以就把他的訊息broadcast出去。
                // 然後如果is_active，那就把訊息也都broadcast給其他也都是is_active的。
                else {
                    time_t now;
                    time(&now);
                    struct tm *local_time = localtime(&now);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%a %b %d %H: %M:%S %Y", local_time);

                    char broadcast_msg[4096];
                    snprintf(broadcast_msg, sizeof(broadcast_msg), "%s : %s said %s\n", time_str, clients[find_idx].name, received);
                    // 開始broadcast
                    for(int i = 0; i < 1024; i++) {
                        if(i != find_idx && clients[i].is_active) {
                            sendto(socket_listen, broadcast_msg, strlen(broadcast_msg), 0, (struct sockaddr *) &clients[i].address, clients[i].addr_len);
                        }
                    }
                }
            }
        }
    }
    printf("Closing listening socket...\n");
    CLOSESOCKET(socket_listen);
    #if defined(_WIN32)
    WSACleanup();
    #endif
    printf("Finished.\n");   
    return 0;
}