#include <stdio.h>
#include <stdlib.h>
// socket library
#include <sys/socket.h>
// for getifaddr, freeifaddr, getnameinfo.
#include <netdb.h>
#include <ifaddrs.h>

int main() {
    // create a linked list struct that stores addresses
    struct ifaddrs *addresses;
    int get_result = getifaddrs(&addresses);
    // if result == -1 means there must be something wrong
    if(get_result == -1) {
        printf("Cannot perform getifaddrs.\n");
        return -1;
    }
    // declare a pointer to move around, firstly point at the begining of the array
    struct ifaddrs *address = addresses;
    while(address) {
        if(addresses->ifa_addr == NULL) {
            address = address->ifa_next;
            continue;
        }

        int addr_family = address->ifa_addr->sa_family;
        if (addr_family == AF_INET || addr_family == AF_INET6) {
            printf("Adapter name: %s\n", address->ifa_name);
            printf("\t%s\t", addr_family == AF_INET ? "IPv4" : "IPv6");

            char ip_addr[100];
            const int addr_family_size = addr_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
            // input: address->ifa_addr, addr_family_size.
            // output: translate the binary ip+port numbers -> human readable things -> we need to declare an array to catch it!!
            getnameinfo(address->ifa_addr, addr_family_size, ip_addr, sizeof(ip_addr), 0, 0, NI_NUMERICHOST);
            printf("\t%s\n", ip_addr);
        }
        address = address->ifa_next;
    }
    // since getifaddr will init a linked list, which will occupy memory.
    // to be a good coder, we need to release them after we finish using it.
    freeifaddr(addresses);
    return 0;
}