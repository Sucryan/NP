#include "sock_compact.h"
#include <time.h>
#if defined (_WIN32)
    WSADATA sock_data;
    if (WSAStartup(MAKEWORD(2, 2), &sock_data))
    {
        fprintf(stderr, "Failed to initialize WinSock2\n");
        return 1;
    }
#endif
// 流程跟client不一樣。
// client: getaddrinfo -> create socket -> connect.
// server: getaddrinfo -> create socket -> bind -> listen -> accept.
int main(int argc, char* argv[]) {
    // 跟client一樣設定hint
    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;      // 告訴hints說這個socket等等是要負責bind+listen的。
    hints.ai_protocol = IPPROTO_TCP;
    
    struct addrinfo *bind_address;
    // 透過前面設定AI_PASSIVE，讓設定0代表預設 aka 0.0.0.0。
    getaddrinfo(0, "8023", &hints, &bind_address);

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

    // 標註這個東西是負責listen的。
    printf("Listening...\n");
    // 10代表buffer size，在accept處理前會先存在那邊，超過他才會丟掉。
    // 在buffer裡面我們會先做three-way handshake。
    int listen_result = listen(
        socket_listen,
        10
    );
    // 用fd_set，主要就是等等可以看哪些人是亮著的。
    // 需要用兩個fd_set是因為一個是看誰是連著的，一個是看有沒有新的訊號進來。
    // 而看新的訊號那個因為會被一直修改，所以必須要用額外的所謂active_socket去存著所有連著的。
    fd_set active_sockets;
    FD_ZERO(&active_sockets);
    FD_SET(socket_listen, &active_sockets); // FD_SET先設定socket_listen是1，其他人進來也會被設定是1
    SOCKET max_socket = socket_listen;      // 當前的max是這個listen，後續有其他連線進來會變大。
    // 因為我們需要存fd和name的對應，這樣才可以知道叫做什麼名字的人說了啥話。
    // 之所以設定1024是因為fd_set最大是1024（更大要用epoll）
    char names[1024][256] = {0};

    printf("Waiting for connections...\n");
    while(1) {
        fd_set read_ready;
        read_ready = active_sockets;
        // 他會去看所有read_ready，也就是本來倍init為active_socket那些人實際上現在到底有沒有傳送訊號進來。
        // 有的話那他會讓他還是維持著1，沒有的話會讓他變成0。
        // 最後面那邊設定為0 -> NULL，也就是他會死等在這邊。
        // 以server來說是合理的，因為你總得有人連線進來再說吧。
        int read_select_result = select(
            max_socket+1,
            &read_ready,
            0,
            0,
            0
        );
        if(read_select_result < 0){
            fprintf(stderr, "select() failed. (%d)\n", GETSOCKETERRNO());
            return 1;
        }
        // iterator
        SOCKET each_socket;
        for (each_socket = 1; each_socket <= max_socket; ++each_socket) {
            // 看哪些人目前狀態是亮著的，代表有訊號進來。
            if (FD_ISSET(each_socket, &read_ready)) {
                if(each_socket == socket_listen) {
                    // 如果socket_listen == 1，代表現在有新的連線。
                    // 用sockaddr來存。
                    struct sockaddr_storage client_address;
                    socklen_t client_address_length = sizeof(client_address);
                    // 先accept, 然後因為accept以後他會額外開一個socket給他，把listen release出來繼續聽。
                    // 所以要額外開一個socket_client去接當前的socket return值。
                    SOCKET socket_client = accept(
                        socket_listen,
                        (struct sockaddr*) &client_address,
                        &client_address_length
                    );
                    if(!ISVALIDSOCKET(socket_client)) {
                        fprintf(stderr, "accept failed. (%d)\n", GETSOCKETERRNO());
                        return 1;
                    }
                    // 設定active_socket這個值為1，反正他會一直push進去。
                    // 並且之所以要設定在active_sockets是因為以後我們每次考慮read_ready都一定要想看看這個人有沒有傳東西進來。
                    FD_SET(socket_client, &active_sockets);
                    if(socket_client > max_socket) {
                        max_socket = socket_client; // 更新max，如果沒有人退出聊天室的話應該會持續增多。
                    }
                    char client_address_literal[100];
                    getnameinfo(
                        (struct sockaddr*) &client_address,
                        client_address_length,
                        client_address_literal,
                        sizeof(client_address_literal),
                        0,
                        0,
                        NI_NUMERICHOST
                    );
                    printf("New connection from %s\n", client_address_literal);
                    // 然後剛連進來的人因為我們在client有寫，他連線成功會把它的名字送過來。
                    char user_name[257]; // 註：或許可能會有問題，可能可以想想至少要在ui告訴使用者名字最長多長？
                    char greeting_msg[1024]; // 註：同上，但暫時這樣測試應該沒差。
                    int bytes_received = recv(socket_client, 
                        user_name, 
                        256, 
                        0
                    );
                    // 代表對方退出了。
                    if(bytes_received < 1) {
                        // 把那個位置清掉，給其他人用。
                        FD_CLR(socket_client, &active_sockets);
                        CLOSESOCKET(socket_client);
                        names[socket_client][0] = '\0';
                        continue;
                    }
                    // 不接會髒髒的
                    if(bytes_received > 0) {
                        user_name[bytes_received] = '\0';
                    }
                    // 要先在名字後面補\0，不然等等可能會有亂碼。
                    // 之所以剛剛在user_name宣告257是因為假設名字真的剛好256，那至少要多一個位置給我存\0。
                    user_name[bytes_received] = '\0'; 
                    // 用snprintf去組成字串
                    snprintf(greeting_msg, sizeof(greeting_msg), "Hello %s, to send message, enter text followed by enter.\n", user_name);
                    // 送回去給client
                    send(socket_client, greeting_msg, strlen(greeting_msg), 0);
                    // 初始化(把第一個位置設定為\0，邏輯上就乾淨了)
                    names[socket_client][0] = '\0';
                    // 塞進去
                    strncpy(names[socket_client], user_name, strlen(user_name));
                }
                else {
                    char received[4096];
                    int bytes_received = recv(
                        each_socket,
                        received,
                        4096,
                        0
                    );
                    // 代表對方退出了。
                    if(bytes_received < 1) {
                        FD_CLR(each_socket, &active_sockets);
                        CLOSESOCKET(each_socket);
                        names[each_socket][0] = '\0';
                        continue;
                    }
                    // 不接會髒髒的
                    if(bytes_received > 0) {
                        received[bytes_received] = '\0';
                    }
                    char to_send[4096];
                    // 處理時間
                    time_t now;
                    time(&now);
                    // 把秒數換成本地時間這種結構
                    struct tm *local_time = localtime(&now);
                    // 準備string裝時間
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%a %b %d %H:%M:%S %Y", local_time);
                    snprintf(to_send, sizeof(to_send), "%s : %s said: %s\n", 
                        time_str,
                        names[each_socket],
                        received
                    );
                    // 在後台print一下，不然不知道發生了啥事。
                    printf("%s\n", to_send);
                    // broadcast他傳的訊息給所有人
                    for(int i = 1; i <= max_socket; i++) {
                        if(FD_ISSET(i, &active_sockets)) {
                            // 這邊代表只要不是listen_socket或者送訊息過來的人，就broadcast出去。
                            if(i != socket_listen && i != each_socket) {
                                int send_result = send(i, to_send, strlen(to_send), 0);
                            }
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