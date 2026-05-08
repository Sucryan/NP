#include "sock_compat.h"

const char *print_name(
    const unsigned char *dns_message,
    const unsigned char *current_pointer,
    const unsigned char *end_of_message
) 
{
    if (current_pointer + 2 > end_of_message)
    {
        fprintf(stderr, "End of DNS Message.\n");
        exit(1);
    }

    if ( (*current_pointer & 0xC0 )  == 0xC0)
    {
        const int offset = ( (*current_pointer & 0x3F) << 8) + current_pointer[1];
        current_pointer += 2;
        printf(" (Name Pointer %d) ", offset);
        print_name(dns_message, dns_message + offset, end_of_message);
        return current_pointer;
    }
    else
    {
        const int length = *current_pointer++;
        if (current_pointer + length + 1 > end_of_message)
        {
            fprintf(stderr, "End of DNS Message.\n");
            exit(1);
        }
        printf("%.*s", length, current_pointer);
        current_pointer += length;
        if (*current_pointer)
        {
            printf(".");
            return print_name(dns_message, current_pointer, end_of_message);
        }
        else
        {
            return current_pointer + 1;
        }
    }
}

void print_dns_message(const char *dns_message, int dns_message_length)
{
    if (dns_message_length < 12)
    {
        fprintf(stderr, "Message is too short to be valid.\n");
        exit(1);
    }

    const unsigned char *header_pointer = (const unsigned char *) dns_message;

    printf("ID = %0X %0X\n", header_pointer[0], header_pointer[1]);

    const int qr = (header_pointer[2] & 0x80) >> 7;
    printf("QR = %d %s\n", qr, qr ? "Response" : "Query");

    const int opcode = (header_pointer[2] & 0x78) >> 3;
    printf("OPCODE = %d ", opcode);
    switch(opcode)
    {
        case 0:
        {
            printf("Standard\n");
            break;
        }
        case 1:
        {
            printf("Reverse\n");
            break;
        }
        case 2:
        {
            printf("Status\n");
            break;
        }
        default:
        {
            printf("Attribute not supported.\n");
            break;
        }
    }

    const int aa = (header_pointer[2] & 0x04) >> 2;
    printf("AA = %d %s\n", aa, aa ? "Authoritative" : "");

    const int tc = (header_pointer[2] & 0x02) >> 1;
    printf("TC = %d %s\n", tc, tc ? "Message Truncated" : "");

    const int rd = (header_pointer[2] & 0x01);
    printf("RD = %d %s\n", rd, rd ? "Recursion Desired" : "");

    if (qr) {
        const int rcode = header_pointer[3] & 0x07;
        printf("RCODE = %d ", rcode);
        switch(rcode) {
            case 0: printf("Query Success\n"); break;
            case 1: printf("Format Error\n"); break;
            case 2: printf("Server Failure\n"); break;
            case 3: printf("Name Error\n"); break;
            case 4: printf("Not Implemented\n"); break;
            case 5: printf("Refused\n"); break;
            default: printf("Attribute not supported\n"); break;
        }
        if (rcode != 0)
        {
            return;
        }
    }

    const int qdcount = (header_pointer[4] << 8) + header_pointer[5];
    const int ancount = (header_pointer[6] << 8) + header_pointer[7];
    const int nscount = (header_pointer[8] << 8) + header_pointer[9];
    const int arcount = (header_pointer[10] << 8) + header_pointer[11];

    printf("QDCOUNT = %d\n", qdcount);
    printf("ANCOUNT = %d\n", ancount);
    printf("NSCOUNT = %d\n", nscount);
    printf("ARCOUNT = %d\n", arcount);

    const unsigned char *content_pointer = header_pointer + 12;
    const unsigned char *end_of_message = header_pointer + dns_message_length;

    if (qdcount)
    {
        for (int qd = 0; qd < qdcount; ++qd)
        {
            if (content_pointer >= end_of_message)
            {
                fprintf(stderr, "End of message.\n");
                exit(1);
            }

            printf("Query %2d\n", qd + 1);
            printf(" Name: ");

            content_pointer = print_name(dns_message, content_pointer, end_of_message);
            printf("\n");

            if (content_pointer + 4 > end_of_message)
            {
                fprintf(stderr, "End of Message.\n");
                exit(1);
            }

            const int type = (content_pointer[0] << 8) + content_pointer[1];
            printf("  Type: %d\n", type);
            content_pointer += 2;

            const int qclass = (content_pointer[0] << 8) + content_pointer[1];
            printf(" Class: %d\n", qclass);
            content_pointer += 2;
        }
    }

    if (ancount || nscount || arcount)
    {
        for (int ad = 0; ad < ancount + nscount + arcount; ad++)
        {
            if (content_pointer >= end_of_message)
            {
                fprintf(stderr, "End of message.\n");
                exit(1);
            }

            printf("Answer %2d\n", ad + 1);
            printf(" Name: ");

            content_pointer = print_name(dns_message, content_pointer, end_of_message); printf("\n");

            if (content_pointer + 10 > end_of_message)
            {
                fprintf(stderr, "End of message.\n");
                exit(1);
            }

            const int type = (content_pointer[0] << 8) + content_pointer[1];
            printf("  Type: %d\n", type);
            content_pointer += 2;

            const int qclass = (content_pointer[0] << 8) + content_pointer[1];
            printf(" Class: %d\n", qclass);
            content_pointer += 2;

            const unsigned int ttl = (content_pointer[0] << 24) + (content_pointer[1] << 16) +
                (content_pointer[2] << 8) + content_pointer[3];
            printf("   TTL: %u\n", ttl);
            content_pointer += 4;

            const int rdlen = (content_pointer[0] << 8) + content_pointer[1];
            printf(" Received Data Length: %d\n", rdlen);
            content_pointer += 2;

            if (content_pointer + rdlen > end_of_message) {
                fprintf(stderr, "End of message.\n"); exit(1);}

            if (rdlen == 4 && type == 1) {
                /* A Record */
                printf("Address :");
                printf("%d.%d.%d.%d\n", content_pointer[0], content_pointer[1], content_pointer[2], content_pointer[3]);

            } else if (rdlen == 16 && type == 28) {
                /* AAAA Record */
                printf("Address :");
                for (int v6_section = 0; v6_section < rdlen; v6_section+=2) {
                    printf("%02x%02x", content_pointer[v6_section], content_pointer[v6_section+1]);
                    if (v6_section + 2 < rdlen)
                    {
                        printf(":");
                    }
                }
                printf("\n");

            } else if (type == 15 && rdlen > 3) {
                /* MX Record */
                const int preference = (content_pointer[0] << 8) + content_pointer[1];
                printf("  Preference: %d\n", preference);
                printf("MX: ");
                print_name(dns_message, content_pointer+2, end_of_message); printf("\n");

            } else if (type == 16) {
                /* TXT Record */
                printf("TXT: '%.*s'\n", rdlen-1, content_pointer+1);

            } else if (type == 5) {
                /* CNAME Record */
                printf("CNAME: ");
                print_name(dns_message, content_pointer, end_of_message);
                printf("\n");
            }

            content_pointer += rdlen;
        }
    }

    if (content_pointer != end_of_message)
    {
        printf("There is some unread data left over.\n");
    }

    printf("\n");
}

int main(int argc, char* argv[])
{
    if (argc < 4)
    {
        printf("Usage:\n\tdns_query dns_server hostname type\n");
        printf("Example:\n\tdns_query rdns.waynewolf.tw www.nycu AAAA\n");
        exit(0);
    }

    if (strlen(argv[1]) > 255)
    {
        fprintf(stderr, "DNS Server Name too long.\n");
        exit(1);
    }
    else if (strlen(argv[2]) > 255)
    {
        fprintf(stderr, "Hostname too long.\n");
        exit(1);
    }

    char *dns_server = argv[1];
    char *host_to_resolve = argv[2];
    char type;
    if (strcmp(argv[3], "A") == 0) {
        type = 1;
    } else if (strcmp(argv[3], "MX") == 0) {
        type = 15;
    } else if (strcmp(argv[3], "TXT") == 0) {
        type = 16;
    } else if (strcmp(argv[3], "AAAA") == 0) {
        type = 28;
    } else if (strcmp(argv[3], "ANY") == 0) {
        type = 255;
    } else {
        fprintf(stderr, "Unknown type '%s'. Use A, AAAA, TXT, MX, or ANY.",
                argv[3]);
        exit(1);
    }

#if defined (_WIN32)
    WSADATA sock_data;
    if (WSAStartup(MAKEWORD(2, 2), &sock_data))
    {
        fprintf(stderr, "Failed to initialize Winsock.\n");
        return 1;
    }
#endif

    printf("Configuring remote address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *dns_address;
    int getaddr_result = getaddrinfo(
        dns_server,
        "53",
        &hints,
        &dns_address
    );

    if (getaddr_result)
    {
        fprintf(stderr, "getaddrinfo() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }

    printf("Creating socket...\n");
    SOCKET socket_to_dns;
    socket_to_dns = socket(
        dns_address->ai_family,
        dns_address->ai_socktype,
        dns_address->ai_protocol
    );

    if (!ISVALIDSOCKET(socket_to_dns))
    {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }

    char query[1024] = 
    {
        0xAB, 0xCD, /* ID */
        0x01, 0x00, /* Set recursion */
        0x00, 0x01, /* QDCOUNT */
        0x00, 0x00, /* ANCOUNT */
        0x00, 0x00, /* NSCOUNT */
        0x00, 0x00 /* ARCOUNT */
    };

    char *message_pointer = query + 12;

    while(*host_to_resolve)
    {
        char *length = message_pointer;
        message_pointer++;
        if (host_to_resolve != argv[2])
        {
            ++host_to_resolve;
        }

        while(*host_to_resolve && *host_to_resolve != '.') *message_pointer++ = *host_to_resolve++;
        *length = message_pointer - length - 1;
    }

    *message_pointer++ = 0;
    *message_pointer++ = 0x00; /* QTYPE first byte */
    *message_pointer++ = type; /* QTYPE second byte */
    *message_pointer++ = 0x00; /* QCLASS first byte */
    *message_pointer++ = 0x01; /* QCLASS second byte*/

    const int query_size = message_pointer - query;

    int bytes_sent = sendto(
        socket_to_dns,
        query,
        query_size,
        0,
        dns_address->ai_addr,
        dns_address->ai_addrlen
    );
    printf("Sent %d bytes.\n", bytes_sent);
    print_dns_message(query, query_size);

    char received[1024];
    int bytes_received = recvfrom(
        socket_to_dns,
        received,
        1024,
        0,
        0,
        0
    );
    printf("Received %d bytes.\n", bytes_received);
    print_dns_message(received, bytes_received);
    printf("\n");

    freeaddrinfo(dns_address);
    CLOSESOCKET(socket_to_dns);

#if defined(_WIN32)
    WSACleanup();
#endif

    return 0;
}