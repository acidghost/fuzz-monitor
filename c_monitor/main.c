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
#include <linux/limits.h>

#include "sections.h"
#include "graph.h"
#include "bb.h"


#define BUF_SZ              (1024 * 1024)
#define HASH_KEY_SEP        "/"
#define HASH_KEY_SZ         64
#define GRAPH_NO_PRINT      ((void *) -1)

bool keep_running = true;


typedef struct monitor {
    char const ** sut;
    HashTable *branch_hits;
    section_bounds_t *sec_bounds;
    basic_block_t *bbs;
    size_t bbs_n;
    char *graph_indiv_path;
    size_t input_n;
} monitor_t;


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


void graph_print_and_free(uint64_t *from, uint64_t **connections,
                          size_t connections_size, void *fd)
{
    if (fd == NULL || fd == GRAPH_NO_PRINT) {
        if (fd == NULL) {
            LOG_I("0x%010" PRIx64 " %zu", *from, connections_size);
        }
        for (size_t i = 0; i < connections_size; i++) {
            free(connections[i]);
        }
    } else {
        FILE *file = (FILE *) fd;
        for (size_t i = 0; i < connections_size; i++) {
            fprintf(file, "\t\"0x%" PRIx64 "\" -> \"0x%" PRIx64 "\";\n",
                *from, *connections[i]);
            free(connections[i]);
        }
    }
    free(from);
}


static void free_hashtable(HashTable *table, char *graph_filename)
{
    FILE *graph_file = NULL;
    Graph *graph = NULL;
    if (graph_filename != NULL) {
        if ((graph_file = fopen(graph_filename, "w")) == NULL) {
            PLOG_F("failed to open file %s", graph_filename);
            exit(EXIT_FAILURE);
        }
        assert(graph_new(&graph) == CC_OK);
        fprintf(graph_file, "digraph {\n");
    }

    // this fixes a segfault when iterating an empty HashTable
    if (hashtable_size(table) == 0) {
        hashtable_destroy(table);
        if (graph != NULL) {
            fprintf(graph_file, "}\n");
            fclose(graph_file);
            graph_destroy(graph);
        }
        return;
    }

    HashTableIter hti;
    hashtable_iter_init(&hti, table);
    TableEntry *entry;
    while (hashtable_iter_next(&hti, &entry) != CC_ITER_END) {
        if (graph_filename != NULL) {
            char *dup = strdup((char *) entry->key);
            char *split = strstr(dup, HASH_KEY_SEP);
            char *second = split + 1;
            *split = '\0';
            uint64_t *from = malloc(sizeof(uint64_t));
            *from = atol(dup);
            uint64_t *to = malloc(sizeof(uint64_t));
            *to = atol(second);
            assert(graph_add(graph, from, to) == CC_OK);
            fprintf(graph_file,
                "\t\"0x%" PRIx64 "\" -> \"0x%" PRIx64 "\" [label=\"%" PRIu64 "\"];\n",
                *from, *to, *(uint64_t *) entry->value);
            free(dup);
        }
        free(entry->key);
        free(entry->value);
    }
    hashtable_destroy(table);

    if (graph_filename != NULL) {
        fprintf(graph_file, "}\n");
        fclose(graph_file);
        LOG_I("graph with %zu nodes and %zu edges",
            graph_nodes(graph), graph_edges(graph));
        graph_foreach(graph, NULL, graph_print_and_free);
        graph_destroy(graph);
    }
}


static int process_branches(bts_branch_t *bts_start, uint64_t count, monitor_t *monitor,
                            uint64_t *new_branches, uint64_t *filtered_count)
{
    if (new_branches == NULL || filtered_count == NULL)
        return 0;

    uint64_t _new_branches = 0;
    uint64_t _filtered_count = 0;

    const section_bounds_t *sec_bounds = monitor->sec_bounds;
    const uint64_t sec_start = sec_bounds ? sec_bounds->sec_start : 0;
    const uint64_t sec_end = sec_bounds ? sec_bounds->sec_end : 0;

    Graph *graph = NULL;
    if (monitor->graph_indiv_path != NULL) {
        assert(graph_new(&graph) == CC_OK);
    }

    for (uint64_t i = 0; i < count; i++) {
        bts_branch_t branch = bts_start[i];
        if (branch.from > 0xFFFFFFFF00000000 || branch.to > 0xFFFFFFFF00000000) {
            continue;
        }

        if (sec_bounds && (
                (branch.from < sec_start || branch.from > sec_end)
                || (branch.to < sec_start || branch.to > sec_end)
            )
        ) continue;

        _filtered_count++;

        uint64_t *from_bb = malloc(sizeof(uint64_t));
        uint64_t *to_bb = malloc(sizeof(uint64_t));
        *from_bb = 0;
        *to_bb = 0;
        for (size_t j = 0; j < monitor->bbs_n; j++) {
            basic_block_t bb = monitor->bbs[j];
            if (branch.from >= bb.from && branch.from < bb.to)
                *from_bb = bb.from;
            if (branch.to >= bb.from && branch.to < bb.to)
                *to_bb = bb.from;
            if (*from_bb != 0 && *to_bb != 0)
                break;
        }
        if (*from_bb == 0)
            *from_bb = branch.from;
        if (*to_bb == 0)
            *to_bb = branch.to;

        if (graph != NULL)
            assert(graph_add(graph, from_bb, to_bb) == CC_OK);

        void *key = malloc(HASH_KEY_SZ * sizeof(char));
        assert(key != NULL);
        snprintf(key, HASH_KEY_SZ, "%" PRIu64 HASH_KEY_SEP "%" PRIu64, *from_bb, *to_bb);
        if (graph == NULL) {
            free(from_bb);
            free(to_bb);
        }

        uint64_t *value = NULL;
        if (hashtable_get(monitor->branch_hits, key, (void **) &value) == CC_OK) {
            (*value)++;
            free(key);
        } else {
            value = malloc(sizeof(uint64_t));
            assert(value != NULL);
            *value = 1;
            if (hashtable_add(monitor->branch_hits, key, value) != CC_OK) {
                LOG_F("failed to add branch [%s]", key);
                return -1;
            }
            _new_branches++;
        }
    }

    if (graph != NULL) {
        if (_new_branches > 0) {
            char graph_indiv_path[PATH_MAX];
            snprintf(graph_indiv_path, PATH_MAX, "%s/graph.%zu.gv",
                monitor->graph_indiv_path, monitor->input_n);

            FILE *graph_file = fopen(graph_indiv_path, "w");
            if (graph_file == NULL) {
                PLOG_F("failed to open file %s", graph_indiv_path);
                return -1;
            }
            fprintf(graph_file, "digraph {\n");
            graph_foreach(graph, graph_file, graph_print_and_free);
            fprintf(graph_file, "}\n");
            fclose(graph_file);
        } else {
            graph_foreach(graph, GRAPH_NO_PRINT, graph_print_and_free);
        }
        graph_destroy(graph);
    }

    *new_branches = _new_branches;
    *filtered_count = _filtered_count;

    return 0;
}


int cmp_uint64(const void *n1, const void *n2)
{
    const uint64_t _n1 = *(const uint64_t *) n1;
    const uint64_t _n2 = *(const uint64_t *) n2;
    if (_n1 > _n2)
        return 1;
    else if (_n1 < _n2)
        return -1;
    else
        return 0;
}


static int monitor_loop(monitor_t *monitor, void *receiver, bool print_seen_inputs)
{
    HashTableConf seen_inputs_table_conf;
    hashtable_conf_init(&seen_inputs_table_conf);
    seen_inputs_table_conf.hash = GENERAL_HASH;
    seen_inputs_table_conf.key_compare = cmp_uint64;
    HashTable *seen_inputs_table = NULL;
    assert(hashtable_new_conf(&seen_inputs_table_conf, &seen_inputs_table) == CC_OK);
    double seen_avg = 0;
    size_t seen_total = 0;

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
        seen_total++;

        uint64_t *buf_hash = malloc(sizeof(uint64_t));
        *buf_hash = hashtable_hash(buf, KEY_LENGTH_VARIABLE, 42);
        uint32_t *seen_inputs_value = NULL;
        if (hashtable_get(seen_inputs_table, buf_hash, (void **) &seen_inputs_value) == CC_OK) {
            (*seen_inputs_value)++;
            free(buf_hash);
        } else {
            seen_inputs_value = malloc(sizeof(uint32_t));
            assert(seen_inputs_value != NULL);
            *seen_inputs_value = 1;
            assert(hashtable_add(seen_inputs_table, buf_hash, seen_inputs_value) == CC_OK);
        }
        seen_avg = seen_total / (double) hashtable_size(seen_inputs_table);
        if (seen_avg > 2)
            seen_total = hashtable_size(seen_inputs_table);

        bts_branch_t *bts_start;
        uint64_t count;
        long start_ms = get_time_ms();
        if (perf_monitor_api(buf, size, monitor->sut, &bts_start, &count) == PERF_FAILURE) {
            LOG_F("failed perf monitoring");
            ret = EXIT_FAILURE;
            break;
        }
        long elapsed_ms = get_time_ms() - start_ms;

        uint64_t new_branches = 0, filtered_count = 0;
        if (process_branches(bts_start, count, monitor, &new_branches, &filtered_count) == -1) {
            ret = EXIT_FAILURE;
            break;
        }

        LOG_I("%8" PRIu64 " %8" PRIu64 " %6" PRIu64 " %8ldms %6" PRIu32 " %4.3g",
            count, filtered_count, new_branches, elapsed_ms, *seen_inputs_value, seen_avg);
        monitor->input_n++;
    }

    HashTableIter hti;
    hashtable_iter_init(&hti, seen_inputs_table);
    TableEntry *seen_inputs_entry;
    while (hashtable_iter_next(&hti, &seen_inputs_entry) != CC_ITER_END) {
        if (print_seen_inputs) {
            LOG_I("%10" PRIx64 " %5" PRIu32,
                *(uint64_t *) seen_inputs_entry->key,
                *(uint32_t *) seen_inputs_entry->value);
        }
        free(seen_inputs_entry->key);
        free(seen_inputs_entry->value);
    }
    hashtable_destroy(seen_inputs_table);

    return ret;
}


void free_monitor(monitor_t *monitor)
{
    if (monitor->sec_bounds)
        free(monitor->sec_bounds);
    if (monitor->bbs)
        free(monitor->bbs);
    free(monitor);
}


void usage(const char *progname)
{
    printf("usage: %s [-g graph.gv] [-t path] [-s .section] [-i] -b r2bb.sh "
           "-- command [args]\n", progname);
}


int main(int argc, char const *argv[])
{
    log_level = INFO;
    char *graph_filename = NULL;
    char *sec_name = NULL;
    bool print_seen_inputs = false;
    char *basic_block_script = NULL;

    monitor_t *monitor = malloc(sizeof(monitor_t));
    assert(monitor != NULL);
    memset(monitor, 0, sizeof(monitor_t));

    int opt;
    while ((opt = getopt(argc, (char * const *) argv, "g:s:ib:t:")) != -1) {
        switch (opt) {
        case 'g':
            graph_filename = optarg;
            break;
        case 's':
            sec_name = optarg;
            break;
        case 'i':
            print_seen_inputs = true;
            break;
        case 'b':
            basic_block_script = optarg;
            break;
        case 't':
            monitor->graph_indiv_path = optarg;
        // default:
        //     free_monitor(monitor);
        //     usage(argv[0]);
        //     exit(EXIT_FAILURE);
        }
    }

    if (argc == optind || basic_block_script == NULL) {
        free_monitor(monitor);
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    monitor->sut = argv + optind;

    if (sec_name) {
        monitor->sec_bounds = malloc(sizeof(section_bounds_t));
        assert(monitor->sec_bounds != NULL);
        int64_t sec_size = section_find(monitor->sut[0], sec_name, monitor->sec_bounds);
        if (sec_size <= 0) {
            if (sec_size == 0)
                LOG_W("%s has no %s section or it's empty", monitor->sut[0], sec_name);
            free_monitor(monitor);
            exit(EXIT_FAILURE);
        }

        LOG_I("monitoring on %s (%s 0x%" PRIx64 " 0x%" PRIx64 " %" PRIi64 ")",
            monitor->sut[0], sec_name, monitor->sec_bounds->sec_start,
            monitor->sec_bounds->sec_end, sec_size);
    } else {
        LOG_I("monitoring on %s (all code)", monitor->sut[0]);
    }

    monitor->bbs_n = basic_blocks_find(basic_block_script, monitor->sut[0], &monitor->bbs);
    if (monitor->bbs_n < 0) {
        LOG_F("failed reading basic blocks");
        free_monitor(monitor);
        exit(EXIT_FAILURE);
    }
    LOG_I("found %zd basic blocks", monitor->bbs_n);
    for (size_t i = 0; i < monitor->bbs_n; i++) {
        LOG_D("BB 0x%08" PRIx64 " 0x%08" PRIx64, monitor->bbs[i].from, monitor->bbs[i].to);
    }

    void *context = zmq_ctx_new();
    if (context == NULL) {
        LOG_F("failed to create new zmq context");
        free_monitor(monitor);
        exit(EXIT_FAILURE);
    }
    void *receiver = zmq_socket(context, ZMQ_PULL);
    if (receiver == NULL) {
        PLOG_F("failed to create socket");
        free_monitor(monitor);
        exit(EXIT_FAILURE);
    }
    if (zmq_bind(receiver, "tcp://*:5558") == -1) {
        PLOG_F("failed to connect to socket");
        free_monitor(monitor);
        exit(EXIT_FAILURE);
    }

    LOG_I("listening...");

    int ret = EXIT_FAILURE;
    if (hashtable_new(&monitor->branch_hits) != CC_OK) {
        LOG_F("failed to create hashtable");
    } else {
        signal(SIGINT, int_sig_handler);
        ret = monitor_loop(monitor, receiver, print_seen_inputs);
        free_hashtable(monitor->branch_hits, graph_filename);
    }

    zmq_close(receiver);
    zmq_ctx_destroy(context);
    free_monitor(monitor);
    return ret;
}
