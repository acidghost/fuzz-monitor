#define _GNU_SOURCE
#include "perf.h"
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
#include <stdio.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>


#define PERF_MAP_SZ (1024 * 512)
#define PERF_AUX_SZ (1024 * 1024)

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

int32_t perf_bts_type = -1;
enum llevel_t log_level = INFO;


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


static void analyze_bts(int perf_fd, struct perf_event_mmap_page *pem, void *mmap_aux,
                        bts_branch_t **bts_start, uint64_t *count)
{
  uint64_t aux_head = pem->aux_head;
  bts_branch_t *br = (bts_branch_t *) mmap_aux;
  if (bts_start != NULL && count != NULL) {
    *bts_start = br;
    *count = ((bts_branch_t *)(mmap_aux + aux_head) - br);
    LOG_M("[?] %p, %llu, %llu", *bts_start, aux_head, *count);
    return;
  }

  uint64_t counter = 0;
  for (; br < ((bts_branch_t *)(mmap_aux + aux_head)); br++) {
    if (unlikely(br->from > 0xFFFFFFFF00000000) || unlikely(br->to > 0xFFFFFFFF00000000)) {
      continue;
    }
    LOG_D("[%" PRIu64 "] 0x%x -> 0x%x", counter, br->from, br->to);
    LOG_M("branch,%" PRIu64 ",%" PRIu64, br->from, br->to);
    counter++;
  }

  LOG_I("BTS recorded %" PRIu64 " branches", counter);
}


static int32_t perf_parent(pid_t child_pid, bts_branch_t **bts_start, uint64_t *count)
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
  LOG_D("waiting for child PID=%lu", child_pid);
  if (waitpid(child_pid, &status, 0) == -1) {
    PLOG_F("failed waiting for child PID=%lu", child_pid);
    goto bail_perf_wait;
  }
  LOG_I("PID=%lu finished with status %d", child_pid, status);

  analyze_bts(perf_fd, pem, mmap_aux, bts_start, count);
  return PERF_SUCCESS;

bail_perf_wait:
  munmap(mmap_aux, PERF_AUX_SZ);
bail_perf_aux:
  munmap(mmap_buf, PERF_MAP_SZ + getpagesize());
bail_perf_buf:
  close(perf_fd);
bail_perf_open:
  kill(child_pid, 9);
  return PERF_FAILURE;
}


static int32_t perf_child(char const **argv)
{
  int null_fd = open("/dev/null", O_WRONLY);
  if (null_fd == -1) {
    PLOG_F("failed to open /dev/null");
    return PERF_FAILURE;
  }
  // dup2(null_fd, STDOUT_FILENO);
  // dup2(null_fd, STDERR_FILENO);

  execv(argv[0], (char *const *) &argv[0]);
  return PERF_FAILURE;
}


void perf_monitor(char const **argv)
{
  if (!perf_init()) {
    exit(EXIT_FAILURE);
  }

  pid_t pid = fork();
  if (pid < 0) {
    PLOG_F("failed to fork");
    exit(EXIT_FAILURE);
  } else if (pid > 0) {
    if (perf_parent(pid, NULL, NULL) == PERF_FAILURE)
      exit(EXIT_FAILURE);
  } else {
    if (perf_child(argv) == PERF_FAILURE)
      exit(EXIT_FAILURE);
  }
}


int32_t perf_monitor_api(const uint8_t *data, size_t data_count, char const **argv,
                         bts_branch_t **bts_start, uint64_t *count)
{
  if (!perf_init()) {
    return PERF_FAILURE;
  }

  int pipe_fd[2];
  if (pipe(pipe_fd) == -1) {
    PLOG_F("failed to pipe");
    return PERF_FAILURE;
  }

  pid_t pid = fork();
  if (pid < 0) {
    PLOG_F("failed to fork");
    return PERF_FAILURE;
  } else if (pid > 0) {
    close(pipe_fd[0]);    // close in fd
    ssize_t write_sz = write(pipe_fd[1], data, data_count);
    if (write_sz == -1) {
      PLOG_F("failed writing to child");
      return PERF_FAILURE;
    }
    close(pipe_fd[1]);
    return perf_parent(pid, bts_start, count);
  } else {
    close(pipe_fd[1]);    // close out fd
    if (dup2(pipe_fd[0], STDIN_FILENO) == -1) {
      PLOG_F("failed to dup2(%d, %d)", pipe_fd[0], STDIN_FILENO);
      exit(EXIT_FAILURE);
    }
    close(pipe_fd[0]);
    perf_child(argv);
    exit(EXIT_FAILURE);
  }
}
