# NP Midterm
## Client相關說明
### 用法
```bash=
# 預設走TCP
# 編譯：gcc chat_client.c -o chat_client
# 範例
./chat_client 127.0.0.1 sucryan [預設8023] [預設TCP]
# 也可以自己輸入
./chat_client 127.0.0.1 sucryan 8023 TCP
```
- 額外功能
    - 支援UDP
    ```bash=
    ./chat_client sucryan 127.0.0.1 8024 UDP
    ```
### 作法簡介
我的做法簡單來說就是用if, elif, else去判斷當前的狀況，用strcmp去先判斷是TCP? UDP? 還是使用者打錯了。
基本上因為UDP跟TCP尤其以Client來講沒差多少，只有差在recvfrom/sendto，跟最前面在處理hints的時候，所以其實相當於複製貼上
(所以其實嚴格來說應該可以讓他精簡很多，就把那段用判斷之類的改掉就好，但是我原本是先開發TCP後面才去用UDP，就有點不想改了，而且debug上這樣邏輯也比較好處理，只是比較冗長而已)。
## Server 相關說明。
### 用法
```bash=
# TCP那隻
# 編譯：gcc chat_server_tcp.c -o chat_server_tcp
./chat_server_tcp 
# UDP
# 編譯：gcc chat_server_udp.c -o chat_server_udp
./chat_server_udp
```
### 作法簡介
我一開始想說或許也可以像是client一樣分成if-else去處理就好，但突然想到後續或許可以額外加上老師範例的功能，也就是自動切換之類的，而且兩個server如果port不撞然後同時開著的話或許某種程度上也可以在伺服器很卡的時候，使用者自主切換聊天室使用。

## 260412 Future Work
以下是我認為或許可以繼續improve的地方
1. UDP因為沒有connect在照顧她，所以server關掉的時候user還是會卡在那邊，我覺得或許可以透過加入keepalive message之類的去緩解這個狀況。