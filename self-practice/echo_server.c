#include <stdio.h>
#include "sock_compact.h"

int main(int argc, char** argv) {
    // 因為 argv 剛進來都是str，所以要用char*先接下來
    // 通常server的ip會設定為0.0.0.0，因為通常會希望任何網卡只要有人連這個port都收。
    char* port = argv[1];
    // 把human readable的ip+port -> binary(machine readable)
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    // 如果沒有設定AI_PASSIVE，然後getaddrinfo又設定NULL，他就預設會bind在127.0.0.1:port上
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *bind_address;
    // getaddrinfo(ip, port, &hints(你要的搜尋條件)), &bind_address(result))
    getaddrinfo(NULL, port, &hints, &bind_address);
    
    // res從getaddrinfo收到的
    /*
    1. ai_family -> v4? v6?
    2. ai_socktype, ai_protocol -> TCP? UDP?
    3. ai_flags -> 是不是server?
    --- 上面的給create socket用，下面的給bind用 --- 
    因為理論上client是不需要 bind aka 只會需要上面這些條件，讓系統隨便幫她抓一個
    4. ai_addr, ai_addrlen
    */
    // socket create: 三個其實都是int, return 得到的是一個fd。
    SOCKET socket_listen = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);
    // bind: 用fd去找到kernel裡面那個socket object，然後把他的 local address 設定成 address
    // 失敗的話 bind_result 會回傳非0的數字
    int bind_result = bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen);
    // 在這邊以後就用不到addrinfo了
    freeaddrinfo(bind_address);
    // listen: 讓同一個fd(socket_listen)，設定為負責listen的socket
    // 用man listen去看可以發現後面那個相當於是指說後面可以queue多長。
    // 同上，失敗的話listen_result 會回傳非0的值
    int listen_result = listen(socket_listen, 10);

    // 用來接client的address, 等等可以回送回去。
    // sockaddr_storage可以確保空間挖得足夠大，不管是AF_INET, AF_INET6都可以放，到時候再cast成
    // struct sockaddr_in * / sockaddr_in6 * 就都可以
    struct sockaddr_storage client_address;
    socklen_t client_len = sizeof(client_address);
    // 用另外的SOCKET(fd)，去接client，做為accept
    SOCKET client_socket = accept(socket_listen, (struct sockaddr *) &client_address, &client_len);
    // 設定msg去接client傳過來的資料
    char buffer[1024];
    char response[1024];
    // 因為這邊會是blocking，所以後續才需要用不管是fork, select等去處理。
    ssize_t n = recv(client_socket, buffer, sizeof(buffer), 0);
    buffer[n] = '\0';
    printf("client: %s\n", buffer);
    snprintf(response, sizeof(response), "%.*s, too", (int)n, buffer);
    send(client_socket, response, strlen(response), 0);

    // 結束
    CLOSESOCKET(client_socket);
    CLOSESOCKET(socket_listen);
}