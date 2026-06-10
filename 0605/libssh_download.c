#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libssh/libssh.h>

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "Usage: ssh_command hostname port user\n");
        return 1;
    }

    const char *hostname = argv[1];
    const char *port = argv[2];
    const char *user = argv[3];

    ssh_session ssh = ssh_new();
    if (!ssh)
    {
        fprintf(stderr, "Cannot create an SSH session.\n");
        return -1;
    }

    ssh_options_set(ssh, SSH_OPTIONS_HOST, hostname);
    ssh_options_set(ssh, SSH_OPTIONS_PORT_STR, port);
    ssh_options_set(ssh, SSH_OPTIONS_USER, user);

    int ssh_return_status = ssh_connect(ssh);
    if (ssh_return_status != SSH_OK)
    {
        fprintf(stderr, "Cannot connect to SSH server.\n%s\n", ssh_get_error(ssh));
        return -1;
    }

    printf("Connected to %s on port %s.\n", hostname, port);
    printf("Server welcome banner: %s.\n", ssh_get_serverbanner(ssh));

    ssh_key ssh_key;
    if (ssh_get_server_publickey(ssh, &ssh_key) != SSH_OK)
    {
        fprintf(stderr, "Cannot obtain SSH public key.\n%s\n", ssh_get_error(ssh));
        return -1;
    }

    unsigned char *ssh_key_hash;
    size_t ssh_key_hash_length;
    if (ssh_get_publickey_hash(
        ssh_key,
        SSH_PUBLICKEY_HASH_SHA256,
        &ssh_key_hash,
        &ssh_key_hash_length
    ) != SSH_OK)
    {
        fprintf(stderr, "Cannot generate SSH public key hash.\n%s\n", ssh_get_error(ssh));
        return -1;
    }

    printf("Host public key hash:\n");
    ssh_print_hash(SSH_PUBLICKEY_HASH_SHA256, ssh_key_hash, ssh_key_hash_length);

    ssh_clean_pubkey_hash(&ssh_key_hash);
    ssh_key_free(ssh_key);

    /* SSH is using trust first, audit later. */
    /* A known host is considered trusted.*/

    printf("Checking whether the ssh host is a known server.\n");
    enum ssh_known_hosts_e ssh_known_state = ssh_session_is_known_server(ssh);
    switch (ssh_known_state) {
        case SSH_KNOWN_HOSTS_OK: 
            printf("Host Known.\n"); 
            break;
        case SSH_KNOWN_HOSTS_CHANGED: 
            printf("Host Changed.\n"); 
            break;
        case SSH_KNOWN_HOSTS_OTHER: 
            printf("Host Other.\n"); 
            break;
        case SSH_KNOWN_HOSTS_UNKNOWN: 
            printf("Host Unknown.\n"); 
            break;
        case SSH_KNOWN_HOSTS_NOT_FOUND: 
            printf("No host file.\n"); 
            break;
        case SSH_KNOWN_HOSTS_ERROR:
            printf("Host error. %s\n", ssh_get_error(ssh)); 
            return 1;
        default: 
            printf("Error. Known: %d\n", ssh_known_state); 
            return -1;
    }

    /* If a server is not known, it must be recorded into hosts to trust */
    if (ssh_known_state == SSH_KNOWN_HOSTS_CHANGED ||
        ssh_known_state == SSH_KNOWN_HOSTS_OTHER ||
        ssh_known_state == SSH_KNOWN_HOSTS_UNKNOWN ||
        ssh_known_state == SSH_KNOWN_HOSTS_NOT_FOUND) 
    {
        printf("Do you want to accept and remember this host? Y/N\n");
        char answer[10];
        fgets(answer, sizeof(answer), stdin);
        if (answer[0] != 'Y' && answer[0] != 'y') {
            return 0;
        }

        ssh_session_update_known_hosts(ssh);
    }

    /* Login Password */
    printf("Password: ");
    char ssh_password[128];
    fgets(ssh_password, sizeof(ssh_password), stdin);
    ssh_password[strlen(ssh_password) -1] = 0;

    if (ssh_userauth_password(ssh, 0, ssh_password) != SSH_AUTH_SUCCESS)
    {
        fprintf(stderr, "Password mismatched with user: %s\n", user);
        return 0;
    }
    else
    {
        printf("Authenticated sucessful.\n");
    }

    printf("Remote file to download: ");
    char scp_filename[128];
    fgets(scp_filename, sizeof(scp_filename), stdin);
    scp_filename[strlen(scp_filename)-1] = 0;

    ssh_scp scp = ssh_scp_new(ssh, SSH_SCP_READ, scp_filename);
    if (!scp)
    {
        fprintf(stderr, "Cannot create SCP channel.\n%s\n", ssh_get_error(ssh));
        return -1;
    }

    if (ssh_scp_init(scp) != SSH_OK)
    {
        fprintf(stderr, "Cannot init SCP channel.\n%s\n", ssh_get_error(ssh));
        return -1;
    }

    if (ssh_scp_pull_request(scp) != SSH_SCP_REQUEST_NEWFILE)
    {
        fprintf(stderr, "Cannot request for file.\n%s\n", ssh_get_error(ssh));
        return -1;
    }

    int remote_filesize = ssh_scp_request_get_size(scp);
    char *remote_filename = strdup(ssh_scp_request_get_filename(scp));
    int remote_file_permission = ssh_scp_request_get_permissions(scp);

    printf(
        "Downloading file %s (%d bytes, permissions 0%o)\n", 
        remote_filename,
        remote_filesize,
        remote_file_permission
    );

    char *file_buffer = malloc(remote_filesize);
    if (!file_buffer)
    {
        fprintf(stderr, "Cannot allocate enough memory for buffer.\n");
        return -1;
    }

    ssh_scp_accept_request(scp);
    if (ssh_scp_read(scp, file_buffer, remote_filesize) == SSH_ERROR)
    {
        fprintf(stderr, "SCP transfer failed. \n%s\n", ssh_get_error(ssh));
        return 1;
    }

    printf("Received %s:\n", remote_filename);
    printf("%.*s\n", remote_filesize, file_buffer);
    FILE *downloaded_file = fopen(remote_filename, "ab");
    if (!downloaded_file)
    {
        fprintf(stderr, "Cannot save file %s\n", remote_filename);
        return -1;
    }
    fwrite(file_buffer, 1, remote_filesize, downloaded_file);
    fflush(downloaded_file);
    fclose(downloaded_file);
    
    ssh_scp_close(scp);
    ssh_scp_free(scp);

    ssh_disconnect(ssh);
    ssh_free(ssh);

    return 0;
}