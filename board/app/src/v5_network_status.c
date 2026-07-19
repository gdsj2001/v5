#include "v5_network_status.h"

#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#endif

int v5_network_status_read_ipv4(const char *interface_name, char *out, size_t out_cap)
{
#ifdef _WIN32
    (void)interface_name;
    if (out && out_cap > 0U) {
        out[0] = '\0';
    }
    return 0;
#else
    struct ifaddrs *interfaces = NULL;
    const struct ifaddrs *entry;
    int found = 0;

    if (out && out_cap > 0U) {
        out[0] = '\0';
    }
    if (!interface_name || !interface_name[0] || !out || out_cap == 0U) {
        return 0;
    }
    if (getifaddrs(&interfaces) != 0) {
        return 0;
    }
    for (entry = interfaces; entry; entry = entry->ifa_next) {
        const struct sockaddr_in *address;

        if (!entry->ifa_name || strcmp(entry->ifa_name, interface_name) != 0 ||
            !entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        address = (const struct sockaddr_in *)entry->ifa_addr;
        if (address->sin_addr.s_addr == htonl(INADDR_ANY)) {
            continue;
        }
        if (inet_ntop(AF_INET, &address->sin_addr, out, out_cap)) {
            found = 1;
            break;
        }
    }
    freeifaddrs(interfaces);
    return found;
#endif
}
