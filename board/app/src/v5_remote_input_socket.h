#ifndef V5_REMOTE_INPUT_SOCKET_H
#define V5_REMOTE_INPUT_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

int v5_remote_input_socket_begin(int fd, const char *http_request);
int v5_remote_input_socket_fd(void);
void v5_remote_input_socket_process(void);
void v5_remote_input_socket_close(void);
int v5_remote_input_socket_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
