// cc libssh_command.c $(pkg-config --cflags --libs libssh) -o libssh_command
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libssh/libssh.h>

int main(int argc, char* argv[]) {
    if (argc < 4) { 
        fprintf(stderr, "Usage: ssh_command hostname port user.\n");
        return 1;
    }
    // 吃argv進來的參數
    const char *hostname = argv[1];
    const char *port = argv[2];
    const char *user = argv[3];
    // 先create session
    ssh_session ssh = ssh_new();
    if (!ssh) {
        fprintf(stderr, "Cannot create an SSH connection.\n");
        return -1;
    }
    // SSH_OPTIONS_xxx是enum，他這邊只是傳int進去讓ssh_options_set知道現在在設定哪個參數。
    ssh_options_set(ssh, SSH_OPTIONS_HOST, hostname);
    ssh_options_set(ssh, SSH_OPTIONS_PORT_STR, port);
    ssh_options_set(ssh, SSH_OPTIONS_USER, user);
    
    // 建立連線
    int ssh_return_status = ssh_connect(ssh);
    if (ssh_return_status != SSH_OK) {
        fprintf(stderr, "Cannot connect to the SSH server.\n");
        return -1;
    }
    
    // 連上
    printf("Connected to %s on port %s.\n", hostname, port);
    printf("Server Welcome Banner: %s.\n", ssh_get_serverbanner(ssh)); // welcome to ... version...那些
    
    // 輸出key info
    printf("Checking whether the ssh host is a known sever.\n");
    enum ssh_server_known_e ssh_known_state = ssh_session_is_known_server(ssh);
    switch (ssh_known_state) {
        case SSH_KNOWN_HOSTS_OK:
            printf("Host Knwon.\n");
            break;
        case SSH_KNOWN_HOSTS_CHANGED:
            printf("Host changed.\n");
            break;
        case SSH_KNOWN_HOSTS_UNKNOWN:
            printf("New Host / Host Unknown.\n");
            break;
        case SSH_KNOWN_HOSTS_NOT_FOUND:
            printf("No SSH hostfile.\n");
            break;
        case SSH_KNOWN_HOSTS_ERROR:
            printf("Host error: %s\n", ssh_get_error(ssh));
            return 1;
        default:
            printf("Error. %d\n", ssh_known_state);
            return 1;
    }

    if(
        ssh_known_state == SSH_KNOWN_HOSTS_CHANGED ||
        ssh_known_state == SSH_KNOWN_HOSTS_UNKNOWN ||
        ssh_known_state == SSH_KNOWN_HOSTS_NOT_FOUND
    ) {
        printf("Do you want to accept and remember this host? (Y/N)\n");
        char answer[10];
        fgets(answer, sizeof(answer), stdin);
        if (answer[0] != 'Y' && answer[0] != 'y') {
            return 0;
        }
        // 新增 user 到 database 中
        ssh_session_update_known_hosts(ssh);
    }

    // 輸入密碼。
    printf("Password: ");
    char ssh_password[128];
    fgets(ssh_password, sizeof(ssh_password), stdin); // 因為fgets連\n也會get
    ssh_password[strlen(ssh_password)-1] = 0; // -> 所以我們把\n給0掉
    
    // 確認是否正確。
    if (
        ssh_userauth_password(
            ssh, 0, ssh_password
        ) != SSH_AUTH_SUCCESS
    ) {
        fprintf(stderr, "Password mismatched with user: %s", user);
        return 0;
    }
    else {
        printf("Authenticated Successful.\n");
    }

    // 建立傳 command 的 channel
    ssh_channel access_channel = ssh_channel_new(ssh);
    // 可能是因為你的電腦ram或者資源不夠。
    if(!access_channel) {
        fprintf(stderr, "Cannot open SSH access channel.\n");
        return -1;
    }
    // 可以登進去，但是沒辦法建立 ex. system沒有安裝bash, sh等
    if(ssh_channel_open_session(access_channel) != SSH_OK) {
        fprintf(stderr, "Cannot open SSH access channel.\n");
        return -1;
    }

    // 提示user輸入command，然後幫她把最尾巴的\n去掉，然後傳過去。
    printf("Remote command to execute: ");
    char remote_command[128];
    fgets(remote_command, sizeof(remote_command), stdin);
    remote_command[strlen(remote_command)-1] = 0;

    if (
        ssh_channel_request_exec(access_channel, remote_command) != SSH_OK
    ) {
        fprintf(stderr, "Failed to execute remote command.\n");
        return -1;
    }
    // 接收回傳資料
    char remote_result[1024];
    int bytes_received;
    while (
        (bytes_received = ssh_channel_read(
            access_channel,
            remote_result,
            sizeof(remote_result),
            0
        ))
    ) {
        if (bytes_received < 0) {
            fprintf(stderr, "Cannot read from SSH.\n");
            return -1;
        }
        printf("%.*s", bytes_received, remote_result);
    }

    ssh_channel_send_eof(access_channel);
    ssh_channel_close(access_channel);
    ssh_channel_free(access_channel);

    ssh_disconnect(ssh);
    ssh_free(ssh);
}