#ifndef V5_REMOTE_WS_H
#define V5_REMOTE_WS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int v5_remote_ws_accept(int fd, const char *http_request);
int v5_remote_ws_recv_text(int fd, char *buffer, size_t size);
int v5_remote_ws_send_text(int fd, const char *text);

#ifdef __cplusplus
}
#endif

#endif
