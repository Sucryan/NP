#include "sock_compact.h"
#include <ctype.h>
#if defined(_WIN32)
    // windows(DOS): if there is a special key input(ex. ctrl+c, enter, escape, shift...)
    #include <conio.h>
#endif
int main(int argc, char* argv[]){
    // windows 在任何網路動作之前都必須加上這段。
    #if defined(_WIN32)
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d)) {
        fprintf(stderr, "Failed to init WinSock2.\n");
        return 1;
    }
    #endif
    // 輸入1, 2, 3參數分別為ip, name, port, type
    char *dest_host = argv[1];
    char *user_name = argv[2];
    char *dest_port = (argc > 3) ? argv[3] : "8023"; 
    char *type = (argc > 4) ? argv[4] : "TCP";
    if(argc < 3) {
        fprintf(stderr, "usage: chat_client your_name ip [port] [type(TCP/UDP)]");
    }
    // C 不能直接寫兩個string == ，因為他把string當char *
    if(strcmp(type, "TCP") == 0) {
        // 設定hints，讓後面的getaddrinfo有東西可循
        printf("Configuaring remote address...\n");
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; // 表示是v4
        hints.ai_socktype = SOCK_STREAM; // 等等UDP要改
        hints.ai_protocol = IPPROTO_TCP; // 等等UDP要改
        // 把human readable的ip+port改成machine readable的 -- aka socket
        struct addrinfo *dest_address;
        // 註：這邊會用&直接把value改在dest_address
        int getaddr_result = getaddrinfo(dest_host, dest_port, &hints, &dest_address);
        if(getaddr_result) {
            fprintf(stderr, "getaddrinfo() failed.\n");
            return 1;
        }
        printf("Remote Address is: ");
        char dest_addr_buf[10];
        char dest_port_buf[10];
        // 其實這應該是optional，因為這只是把剛剛轉換過去的東西再轉回來而已。
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

        // 建立socket
        printf("Creating socket...\n");
        SOCKET socket_to_dest;
        socket_to_dest = socket(
            dest_address->ai_family,
            dest_address->ai_socktype,
            dest_address->ai_protocol
        );
        if(!ISVALIDSOCKET(socket_to_dest)) {
            fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
            return 1;
        }
        // 連線（client不需要bind）
        printf("Connecting to the Chat Server...\n");
        int connect_result = connect(
            socket_to_dest,
            dest_address->ai_addr,
            dest_address->ai_addrlen
        );
        if (connect_result) {
            fprintf(stderr, "connect failed. (%d)\n", GETSOCKETERRNO());
            return 1;
        }
        // 連上了，可以把這個空間free掉
        freeaddrinfo(dest_address);
        printf("Connected.\n");
        // 先把名字送過去，讓server可以送那個打招呼過來。
        send(socket_to_dest, 
            user_name, 
            strlen(user_name), 
            0
        );
        // 接下來要讓使用者可以輸入東西。
        while(1) {
            // 設定fd_set這個array，同時讓後面的select可以同時監控socket_to_dest跟keyboard input(stdin)。
            fd_set read_ready;
            FD_ZERO(&read_ready);
            FD_SET(socket_to_dest, &read_ready);
            // windows處理keyboard input不是當作file。
            #if !defined(_WIN32) 
            FD_SET(fileno(stdin), &read_ready);
            #endif
            struct timeval select_timeout;
            select_timeout.tv_sec = 0;
            select_timeout.tv_usec = 100000;
            // 重點：只看到socket_to_dest+1這個max的位置，後面都不用看了（總共1024）
            // 然後把read_ready整個塞進去，理論上如果有input的話他就會把stdin那個位置設定為1（fd==0那邊）
            // 然後如果server有送東西過來他就會把socket_to_dest設定為1。
            // 然後用struct timeval去看有沒有超過時間，超過就先跳出來，不要一直block在那邊。
            int select_result = select(socket_to_dest+1, &read_ready, 0, 0, &select_timeout);
            // 如果伺服器有傳東西來呢？
            if (FD_ISSET(socket_to_dest, &read_ready)) {
                char received[4096];
                int bytes_received = recv(
                    socket_to_dest,
                    received,
                    4096,
                    0
                );
                if(bytes_received < 1) {
                    printf("Connection closed by destination.\n");
                    break;
                }
                // 如果不加這段他會因為可能後面的文字比較短，以至於出現髒髒的情況，例如後面的聊天訊息後面都固定接greeting都後半段。
                if(bytes_received > 0) {
                    received[bytes_received] = '\0';
                }
                // 把伺服器處理好的對話內容塞過來，理論上我print出來就好。
                printf("%s\n", received);
            }
            // 同上面，因為windows處理keyboard不是file。
            #if defined(_WIN32)
            if(_kbit()) {
            #else 
            // 如果鍵盤有輸入內容？
            if(FD_ISSET(fileno(stdin), &read_ready)) {
            #endif
                char to_send[4096];
                // 疑問：為啥前面已經有FD_ISSET提醒了，還要這個什麼fget?
                // 解答：fget才是真的去撈資料的人，也就是現在他會去停在那邊等你輸入stdin，然後用to_send去接。
                if(!fgets(to_send, 4096, stdin)){
                    break;
                }
                int byte_send = send(socket_to_dest, 
                    to_send, 
                    strlen(to_send), 
                    0
                );
            }
        }   
        printf("Closing socket...\n");
        CLOSESOCKET(socket_to_dest);
        printf("Finished.\n");
        //  跟WSAStartup對應。
        #if defined(_WIN32)
        WSACleanup(); 
        #endif
        return 0;
    }
    else if(strcmp(type, "UDP") == 0) {
        
    }
    else{
        // 如果輸入不是TCP/UDP，則輸出錯誤訊息。
        fprintf(stderr, "error type input, you must choose from TCP/UDP.");
    }
}