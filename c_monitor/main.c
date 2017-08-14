#define _GNU_SOURCE
#include <zmq.h>
#include <perf/log.h>
#include <perf/perf.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>
#include <hashtable.h>
#include <signal.h>
#include <stdbool.h>


#define BUF_SZ  (1024 * 1024)

bool keep_running = true;


static inline long get_time_ms(void)
{
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return round(spec.tv_nsec / 1.0e6);
}


void int_sig_handler(int signum)
{
    keep_running = false;
}


static void free_hashtable(HashTable *table)
{
    HashTableIter hti;
    hashtable_iter_init(&hti, table);
    TableEntry *entry;
    while (hashtable_iter_next(&hti, &entry) != CC_ITER_END) {
        free(entry->key);
        free(entry->value);
    }
    hashtable_destroy(table);
}


static int monitor_loop(void *receiver, char const **argv, HashTable *branch_hits)
{
    while (keep_running) {
        uint8_t buf[BUF_SZ];
        int size = zmq_recv(receiver, buf, BUF_SZ, ZMQ_DONTWAIT);
        if (size == -1) {
            if (errno == EAGAIN) {
                continue;
            } else {
                LOG_F("failed to receive from zmq");
                return EXIT_FAILURE;
            }
        }

        bts_branch_t *bts_start;
        uint64_t count;
        long start_ms = get_time_ms();
        if (perf_monitor_api(buf, size, argv, &bts_start, &count) == PERF_FAILURE) {
            LOG_F("failed perf monitoring");
            return EXIT_FAILURE;
        }
        long elapsed_ms = get_time_ms() - start_ms;

        uint64_t new_branches = 0;
        for (uint64_t i = 0; i < count; i++) {
            bts_branch_t branch = bts_start[i];
            if (branch.from > 0xFFFFFFFF00000000 || branch.to > 0xFFFFFFFF00000000) {
                continue;
            }
            void *key = malloc(64 * sizeof(char));
            snprintf(key, 64, "%" PRIu64 "/%" PRIu64, branch.from, branch.to);
            void *value;
            if (hashtable_get(branch_hits, key, &value) == CC_OK) {
                *(uint64_t *) value += 1;
            } else {
                value = malloc(sizeof(uint64_t));
                if (hashtable_add(branch_hits, key, value) != CC_OK) {
                    LOG_F("failed to add branch [%s]", key);
                    return EXIT_FAILURE;
                }
                new_branches++;
            }
        }

        LOG_I("%8" PRIu64 " %6" PRIu64 " %8ldms", count, new_branches, elapsed_ms);
    }

    return EXIT_SUCCESS;
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

    HashTable *branch_hits;
    int ret;
    if (hashtable_new(&branch_hits) != CC_OK) {
        LOG_F("failed to create hashtable");
        ret = EXIT_FAILURE;
    } else {
        signal(SIGINT, int_sig_handler);
        ret = monitor_loop(receiver, argv + 1, branch_hits);
        free_hashtable(branch_hits);
    }

    zmq_close(receiver);
    zmq_ctx_destroy(context);
    return ret;
}
