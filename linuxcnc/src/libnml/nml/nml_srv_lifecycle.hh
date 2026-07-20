#ifndef NML_SRV_LIFECYCLE_HH
#define NML_SRV_LIFECYCLE_HH

typedef void (*NML_SERVER_WAIT_STEP)(double seconds);

inline int nml_server_set_is_local_only(int server_count,
	int remote_server_count)
{
    return server_count > 0 && remote_server_count == 0;
}

inline void nml_server_wait_until_shutdown(volatile int *shutdown_requested,
	NML_SERVER_WAIT_STEP wait_step)
{
    while (!*shutdown_requested) {
	wait_step(1.0);
    }
}

#endif
