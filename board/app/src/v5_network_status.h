#ifndef V5_NETWORK_STATUS_H
#define V5_NETWORK_STATUS_H

#include <stddef.h>

#define V5_NETWORK_STATUS_IPV4_CAP 16U

int v5_network_status_read_ipv4(const char *interface_name, char *out, size_t out_cap);

#endif
