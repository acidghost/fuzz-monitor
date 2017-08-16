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
#include <assert.h>

#include "sections.h"
#include "graph.h"


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


void graph_print_and_free(uint64_t *from, uint64_t **connections, size_t connections_size)
{
    LOG_I("0x%010" PRIx64 " %zu", *from, connections_size);
    free(from);
    for (size_t i = 0; i < connections_size; i++) {
        free(connections[i]);
    }
}


static void free_hashtable(HashTable *table, char *out_filename)
{
    FILE *out_file = NULL;
    if (out_filename && (out_file = fopen(out_filename, "w")) == NULL) {
        PLOG_F("failed to open output file %s", out_filename);
        exit(EXIT_FAILURE);
    }

    HashTableIter hti;
    hashtable_iter_init(&hti, table);
    TableEntry *entry;
    Graph *graph = NULL;
    assert(graph_new(&graph) == CC_OK);
    while (hashtable_iter_next(&hti, &entry) != CC_ITER_END) {
        if (out_filename) {
            char *dup = strdup((char *) entry->key);
            char *split = strstr(dup, HASH_KEY_SEP);
            char *second = split + 1;
            *split = '\0';
            uint64_t *from = malloc(sizeof(uint64_t));
            *from = atol(dup);
            uint64_t *to = malloc(sizeof(uint64_t));
            *to = atol(second);
            assert(graph_add(graph, from, to) == CC_OK);
            fprintf(out_file, "0x%010" PRIx64 " -> 0x%010" PRIx64 " %10" PRIu64 "\n",
                *from, *to, *(uint64_t *) entry->value);
            free(dup);
        }
        free(entry->key);
        free(entry->value);
    }
    hashtable_destroy(table);

    LOG_I("graph with %zu nodes and %zu edges", graph_nodes(graph), graph_edges(graph));
    graph_foreach(graph, graph_print_and_free);
    graph_destroy(graph);

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
    HashTableConf seen_inputs_table_conf;
    hashtable_conf_init(&seen_inputs_table_conf);
    seen_inputs_table_conf.hash = GENERAL_HASH;
    HashTable *seen_inputs_table = NULL;
    assert(hashtable_new_conf(&seen_inputs_table_conf, &seen_inputs_table) == CC_OK);

    int ret = EXIT_SUCCESS;
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
                ret = EXIT_FAILURE;
                break;
            }
        }

        uint64_t *buf_hash = malloc(sizeof(uint64_t));
        *buf_hash = hashtable_hash(buf, KEY_LENGTH_VARIABLE, 42);
        uint32_t *seen_inputs_value = NULL;
        if (hashtable_get(seen_inputs_table, buf_hash, (void **) &seen_inputs_table) == CC_OK) {
            (*seen_inputs_value)++;
            free(buf_hash);
        } else {
            seen_inputs_value = malloc(sizeof(uint32_t));
            *seen_inputs_value = 1;
            assert(hashtable_add(seen_inputs_table, buf_hash, seen_inputs_value) == CC_OK);
        }

        bts_branch_t *bts_start;
        uint64_t count;
        long start_ms = get_time_ms();
        if (perf_monitor_api(buf, size, argv, &bts_start, &count) == PERF_FAILURE) {
            LOG_F("failed perf monitoring");
            ret = EXIT_FAILURE;
            break;
        }
        long elapsed_ms = get_time_ms() - start_ms;

        uint64_t new_branches = 0;
        if (process_branches(bts_start, count, branch_hits, sec_filter, &new_branches) == -1) {
            ret = EXIT_FAILURE;
            break;
        }

        LOG_I("%8" PRIu64 " %6" PRIu64 " %8ldms", count, new_branches, elapsed_ms);
    }

    HashTableIter hti;
    hashtable_iter_init(&hti, seen_inputs_table);
    TableEntry *seen_inputs_entry;
    while (hashtable_iter_next(&hti, &seen_inputs_entry) != CC_ITER_END) {
        LOG_I("%" PRIx64 " %" PRIu32, * (uint64_t *) seen_inputs_entry->key,
            * (uint32_t *) seen_inputs_entry->value);
        free(seen_inputs_entry->key);
        free(seen_inputs_entry->value);
    }
    hashtable_destroy(seen_inputs_table);

    return ret;
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
