#define _GNU_SOURCE
#include "log.h"
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>


#define PERF_MAP_SZ (1024 * 512)
#define PERF_AUX_SZ (1024 * 1024)

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

enum llevel_t log_level = INFO;
int32_t perf_bts_type = -1;
bool human_readable = true;


static inline long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                                   int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}


static bool perf_init(void)
{
  int fd = open("/sys/bus/event_source/devices/intel_bts/type", O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    PLOG_F("Intel BTS not supported");
    return false;
  }

  char buf[127];
  ssize_t sz = read(fd, buf, 127);
  if (sz < 0) {
    PLOG_F("failed reading BTS type file");
    close(fd);
    return false;
  }

  perf_bts_type = (int32_t) strtoul(buf, NULL, 10);
  LOG_D("perf_bts_type = %" PRIu32, perf_bts_type);

  close(fd);
  return true;
}


static void analyze_bts(int perf_fd, struct perf_event_mmap_page *pem, void *mmap_aux)
{
  struct bts_branch {
    uint64_t from;
    uint64_t to;
    uint64_t misc;
  };

  uint64_t aux_head = pem->aux_head;
  uint64_t counter = 0;
  struct bts_branch *br = (struct bts_branch *) mmap_aux;
  for (; br < ((struct bts_branch *)(mmap_aux + aux_head)); br++) {
    if (unlikely(br->from > 0xFFFFFFFF00000000) || unlikely(br->to > 0xFFFFFFFF00000000)) {
      continue;
    }
    LOG_D("[%" PRIu64 "] 0x%x -> 0x%x", counter, br->from, br->to);
    LOG_M("branch,%" PRIu64 ",%" PRIu64, br->from, br->to);
    counter++;
  }

  LOG_I("BTS recorded %" PRIu64 " branches", counter);
}


static void do_parent(pid_t child_pid)
{
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(struct perf_event_attr));
  pe.size = sizeof(struct perf_event_attr);
  pe.exclude_kernel = 1;
  pe.type = perf_bts_type;

  int perf_fd = perf_event_open(&pe, child_pid, -1, -1, PERF_FLAG_FD_CLOEXEC);
  if (perf_fd == -1) {
    PLOG_F("perf_event_open() failed");
    goto bail_perf_open;
  }

  void *mmap_buf =
    mmap(NULL, PERF_MAP_SZ + getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED, perf_fd, 0);
  if (mmap_buf == MAP_FAILED) {
    PLOG_F("failed mmap perf buffer, sz=%zu", (size_t) PERF_MAP_SZ + getpagesize());
    goto bail_perf_buf;
  }

  struct perf_event_mmap_page *pem = (struct perf_event_mmap_page *)mmap_buf;
  pem->aux_offset = pem->data_offset + pem->data_size;
  pem->aux_size = PERF_AUX_SZ;
  void *mmap_aux = mmap(NULL, pem->aux_size, PROT_READ, MAP_SHARED, perf_fd, pem->aux_offset);
  if (mmap_aux == MAP_FAILED) {
    PLOG_F("failed mmap perf aux, sz=%zu", (size_t) PERF_AUX_SZ);
    goto bail_perf_aux;
  }

  ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0);

  int status;
  if (waitpid(child_pid, &status, 0) == -1) {
    PLOG_F("failed waiting for child PID=%lu", child_pid);
    goto bail_perf_wait;
  }
  LOG_I("PID=%lu finished with status %d", child_pid, status);

  analyze_bts(perf_fd, pem, mmap_aux);
  return;

bail_perf_wait:
  munmap(mmap_aux, PERF_AUX_SZ);
bail_perf_aux:
  munmap(mmap_buf, PERF_MAP_SZ + getpagesize());
bail_perf_buf:
  close(perf_fd);
bail_perf_open:
  kill(child_pid, 9);
  exit(EXIT_FAILURE);
}


static void do_child(char const **argv)
{
  int null_fd = open("/dev/null", O_WRONLY);
  if (null_fd == -1) {
    PLOG_F("failed to open /dev/null");
    exit(EXIT_FAILURE);
  }
  dup2(null_fd, STDOUT_FILENO);
  dup2(null_fd, STDERR_FILENO);

  size_t idx = human_readable ? 1 : 2;
  execv(argv[idx], (char *const *) &argv[idx]);
  exit(EXIT_FAILURE);
}


int main(int argc, char const **argv)
{
  if (argc < 2) {
    LOG_I("usage: %s [x] command [args]", argv[0]);
    return EXIT_SUCCESS;
  }

  if (argc > 2 && argv[1][0] == 'x' && argv[1][1] == '\0') {
    human_readable = false;
    log_level = MACHINE;
  }

  LOG_I("Starting perf tool...");
  if (!perf_init()) {
    return EXIT_FAILURE;
  }

  pid_t pid = fork();
  if (pid < 0) {
    PLOG_F("failed to fork");
    return EXIT_FAILURE;
  } else if (pid > 0) {
    do_parent(pid);
  } else {
    do_child(argv);
  }
}
