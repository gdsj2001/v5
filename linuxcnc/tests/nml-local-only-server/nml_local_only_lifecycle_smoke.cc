#include "../../src/libnml/nml/nml_srv_lifecycle.hh"

static volatile int shutdown_requested;
static int wait_calls;
static int spawn_calls;

static void request_shutdown(double seconds)
{
    if (seconds != 1.0) {
	wait_calls = -100;
	shutdown_requested = 1;
	return;
    }
    wait_calls++;
    shutdown_requested = 1;
}

static int run_local_only_parent(int server_count, int remote_server_count)
{
    if (!nml_server_set_is_local_only(server_count, remote_server_count)) {
	spawn_calls++;
	return 0;
    }

    nml_server_wait_until_shutdown(&shutdown_requested, request_shutdown);
    return 1;
}

int main()
{
    shutdown_requested = 0;
    wait_calls = 0;
    spawn_calls = 0;

    if (!run_local_only_parent(5, 0)) {
	return 1;
    }
    if (spawn_calls != 0 || wait_calls != 1 || !shutdown_requested) {
	return 2;
    }

    shutdown_requested = 0;
    if (run_local_only_parent(5, 1)) {
	return 3;
    }
    if (spawn_calls != 1 || wait_calls != 1) {
	return 4;
    }

    shutdown_requested = 1;
    wait_calls = 0;
    nml_server_wait_until_shutdown(&shutdown_requested, request_shutdown);
    if (wait_calls != 0) {
	return 5;
    }

    if (nml_server_set_is_local_only(0, 0)) {
	return 6;
    }

    return 0;
}
