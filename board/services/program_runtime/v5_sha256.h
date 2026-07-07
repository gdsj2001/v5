#ifndef V5_SHA256_H
#define V5_SHA256_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void v5_sha256_hex(const unsigned char *data, size_t size, char out_hex[65]);

#ifdef __cplusplus
}
#endif

#endif
