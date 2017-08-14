#ifndef _H_PERF_
#define _H_PERF_

#include <inttypes.h>
#include <stdlib.h>

#define PERF_FAILURE -1
#define PERF_SUCCESS  1

typedef struct bts_branch {
    uint64_t from;
    uint64_t to;
    uint64_t misc;
} bts_branch_t;

typedef struct gbl_status {
    pid_t child_pid;
    int perf_fd;
    size_t data_ready;
    void *mmap_buf;
    void *mmap_aux;
} gbl_status_t;

void perf_monitor(char const **argv);
int32_t perf_monitor_api(const uint8_t *data, size_t data_count, char const **argv,
                         bts_branch_t **bts_start, uint64_t *count);

#endif
