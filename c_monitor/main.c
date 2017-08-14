#define _GNU_SOURCE
#include <zmq.h>
#include <perf/log.h>
#include <perf/perf.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>

#define BUF_SZ  (1024 * 1024)


static inline long get_time_ms(void)
{
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return round(spec.tv_nsec / 1.0e6);
}


int main(int argc, char const *argv[])
{
    if (argc < 2) {
        LOG_E("usage: %s command [args]", argv[0]);
        exit(EXIT_FAILURE);
    }

    log_level = INFO;

    void *context = zmq_ctx_new();
    if (context == NULL) {
        LOG_F("failed to create new zmq context");
        exit(EXIT_FAILURE);
    }
    void *receiver = zmq_socket(context, ZMQ_PULL);
    if (receiver == NULL) {
        PLOG_F("failed to create socket");
        exit(EXIT_FAILURE);
    }
    if (zmq_bind(receiver, "tcp://*:5558") == -1) {
        PLOG_F("failed to connect to socket");
        exit(EXIT_FAILURE);
    }

    LOG_I("listening...");

    while (1) {
        uint8_t buf[BUF_SZ];
        int size = zmq_recv(receiver, buf, BUF_SZ, 0);
        if (size == -1) {
            LOG_F("failed to receive from zmq");
            break;
        }

        bts_branch_t *bts_start;
        uint64_t count;
        long start_ms = get_time_ms();
        if (perf_monitor_api(buf, size, &argv[1], &bts_start, &count) == PERF_FAILURE) {
            LOG_F("failed perf monitoring");
            break;
        }
        long elapsed_ms = get_time_ms() - start_ms;

        LOG_I("%8" PRIu64 " %8ldms", count, elapsed_ms);
    }

    zmq_close(receiver);
    zmq_ctx_destroy(context);
    return 0;
}
