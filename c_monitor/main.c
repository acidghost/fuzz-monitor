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
#include <unistd.h>

#include "sections.h"


#define BUF_SZ  (1024 * 1024)
#define HASH_KEY_SEP "/"
#define HASH_KEY_SZ  64

bool keep_running = true;


typedef struct section_filter {
    uint64_t sec_start;
    uint64_t sec_end;
} section_filter_t;


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


static void free_hashtable(HashTable *table, char *out_filename)
{
    FILE *out_file;
    if (out_filename && (out_file = fopen(out_filename, "w")) == NULL) {
        PLOG_F("failed to open output file %s", out_filename);
        exit(EXIT_FAILURE);
    }

    HashTableIter hti;
    hashtable_iter_init(&hti, table);
    TableEntry *entry;
    while (hashtable_iter_next(&hti, &entry) != CC_ITER_END) {
        if (out_filename) {
            char *dup = strdup((char *) entry->key);
            char *split = strstr(dup, HASH_KEY_SEP);
            char *second = split + 1;
            *split = '\0';
            fprintf(out_file, "0x%010" PRIx64 " -> 0x%010" PRIx64 " %10" PRIu64 "\n",
                atol(dup), atol(second), *(uint64_t *) entry->value);
            free(dup);
        }
        free(entry->key);
        free(entry->value);
    }
    hashtable_destroy(table);

    if (out_filename)
        fclose(out_file);
}


static int process_branches(bts_branch_t *bts_start, uint64_t count, HashTable *branch_hits,
                            section_filter_t *sec_filter, uint64_t *new_branches)
{
    if (new_branches == NULL)
        return 0;

    for (uint64_t i = 0; i < count; i++) {
        bts_branch_t branch = bts_start[i];
        if (branch.from > 0xFFFFFFFF00000000 || branch.to > 0xFFFFFFFF00000000) {
            continue;
        }

        if (sec_filter && (
                (branch.from < sec_filter->sec_start || branch.from > sec_filter->sec_end)
                || (branch.to < sec_filter->sec_start || branch.to > sec_filter->sec_end)
            )
        ) continue;

        void *key = malloc(HASH_KEY_SZ * sizeof(char));
        snprintf(key, HASH_KEY_SZ, "%" PRIu64 HASH_KEY_SEP "%" PRIu64,
            branch.from, branch.to);

        void *value;
        if (hashtable_get(branch_hits, key, value) == CC_OK) {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
            *(uint64_t *) value += 1;
            #pragma GCC diagnostic pop
            free(key);
        } else {
            value = malloc(sizeof(uint64_t));
            *(uint64_t *) value = 1;
            if (hashtable_add(branch_hits, key, value) != CC_OK) {
                LOG_F("failed to add branch [%s]", key);
                return -1;
            }
            (*new_branches)++;
        }
    }

    return 0;
}


static int monitor_loop(void *receiver, char const **argv, HashTable *branch_hits,
                        section_filter_t *sec_filter)
{
    while (keep_running) {
        uint8_t buf[BUF_SZ];
        int size = zmq_recv(receiver, buf, BUF_SZ, ZMQ_DONTWAIT);
        if (size == -1) {
            if (errno == EAGAIN) {
                usleep(100);
                continue;
            } else if (!keep_running) {
                break;
            } else {
                PLOG_F("failed to receive from zmq");
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
        if (process_branches(bts_start, count, branch_hits, sec_filter, &new_branches) == -1)
            return EXIT_FAILURE;

        LOG_I("%8" PRIu64 " %6" PRIu64 " %8ldms", count, new_branches, elapsed_ms);
    }

    return EXIT_SUCCESS;
}


int main(int argc, char const *argv[])
{
    log_level = INFO;
    char *out_filename = NULL;
    char *sec_name = NULL;
    int opt;
    while ((opt = getopt(argc, (char * const *) argv, "f:s:")) != -1) {
        switch (opt) {
        case 'f':
            out_filename = optarg;
            break;
        case 's':
            sec_name = optarg;
            break;
        }
    }

    if (argc == optind) {
        LOG_I("usage: %s [-f outfile] [-s section] -- command [args]", argv[0]);
        exit(EXIT_FAILURE);
    }

    char const **sut = argv + optind;

    section_filter_t filter;
    if (sec_name) {
        int64_t sec_size = section_find(sut[0], sec_name, &filter.sec_start, &filter.sec_end);
        if (sec_size <= 0) {
            if (sec_size == 0)
                LOG_W("%s has no %s section or it's empty", sut[0], sec_name);
            exit(EXIT_FAILURE);
        }

        LOG_I("monitoring on %s (%s 0x%" PRIx64 " 0x%" PRIx64 " %" PRIi64 ")",
            sut[0], sec_name, filter.sec_start, filter.sec_end, sec_size);
    } else {
        LOG_I("monitoring on %s (all code)", sut[0]);
    }

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
        ret = monitor_loop(receiver, sut, branch_hits, sec_name ? &filter : NULL);
        free_hashtable(branch_hits, out_filename);
    }

    zmq_close(receiver);
    zmq_ctx_destroy(context);
    return ret;
}
