#define _GNU_SOURCE
#include "log.h"
#include "perf.h"

#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>


int main(int argc, char const **argv)
{
  if (argc < 2) {
    LOG_I("usage: %s [x] command [args]", argv[0]);
    return EXIT_SUCCESS;
  }

  bool human_readable = true;
  if (argc > 2 && argv[1][0] == 'x' && argv[1][1] == '\0') {
    human_readable = false;
    log_level = MACHINE;
  }

  if (human_readable) {
    LOG_I("Starting perf tool...");
    perf_monitor(&argv[1]);
  } else {
    uint8_t data[512];
    ssize_t read_sz = read(STDIN_FILENO, data, 511);
    if (read_sz < 0) {
      LOG_M("failed reading input");
      exit(EXIT_FAILURE);
    }
    LOG_M("read %zd", read_sz);

    bts_branch_t *bts_start;
    uint64_t count;
    if (perf_monitor_api(data, read_sz, &argv[2], &bts_start, &count) == PERF_FAILURE) {
      LOG_M("failed perf monitoring");
      exit(EXIT_FAILURE);
    }

    LOG_M("count %" PRIu64, count);
    for (bts_branch_t *br = bts_start; br < (bts_start + count); br++) {
      uint64_t i = br - bts_start;
      LOG_M("branch,%"PRIu64"/%"PRIu64",%" PRIu64 ",%" PRIu64, i, count, br->from, br->to);
    }
  }
}
