#include "v5_process_residency.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>

static void raise_memlock_limit(void)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_MEMLOCK, &limit) != 0) {
        return;
    }
    if (limit.rlim_cur == RLIM_INFINITY) {
        return;
    }
    limit.rlim_cur = limit.rlim_max;
    (void)setrlimit(RLIMIT_MEMLOCK, &limit);
}

int v5_process_residency_lock(const char *process_name)
{
    raise_memlock_limit();
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "%s mlockall(MCL_CURRENT|MCL_FUTURE) failed: %s\n",
                process_name ? process_name : "v5", strerror(errno));
        return 0;
    }
    return 1;
}
