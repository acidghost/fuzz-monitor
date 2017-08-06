#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <zmq.h>
#include <unistd.h>


#define PATH_NEEDLE "cur_input"
#define LOG_FILENAME "actions.log"

static pid_t pid;
static int fuzzer_out_fd;
static FILE *log_file;
static long start_time_ms;
static void *context;
static void *sender;

static inline void set_log_file(void)
{
  log_file = fopen(LOG_FILENAME, "w");
  if (!log_file) {
    fprintf(stderr, "Unable to open " LOG_FILENAME "\n");
    abort();
  }
}

static inline long gettimems(void)
{
  struct timespec spec;
  clock_gettime(CLOCK_MONOTONIC, &spec);
  return round(spec.tv_nsec / 1.0e6);
}

static inline void log_action(char *str)
{
  long ms = gettimems() - start_time_ms;
  fprintf(log_file, "%ld\t%s\n", ms, str);
}

typedef int (*open_fn_t)(const char *, int, ...);

static inline int __open(open_fn_t open_fn, const char *path, int flags, va_list args)
{
	if (__OPEN_NEEDS_MODE(flags)) {
		mode_t mode = va_arg(args, mode_t);
		return open_fn(path, flags, mode);
	} else {
		return open_fn(path, flags);
	}
}

int open(const char *path, int flags, ...)
{
	static open_fn_t real_open;
	if (!real_open)
		real_open = dlsym(RTLD_NEXT, "open");

	va_list args;
	va_start(args, flags);
	int ret = __open(real_open, path, flags, args);
	va_end(args);

  if (strstr(path, PATH_NEEDLE)) {
    fuzzer_out_fd = ret;
    log_action("open");
  }

	return ret;
}

int open64(const char *path, int flags, ...)
{
	static open_fn_t real_open64;
	if (!real_open64)
		real_open64 = dlsym(RTLD_NEXT, "open64");

	va_list args;
	va_start(args, flags);
	int ret = __open(real_open64, path, flags, args);
	va_end(args);

  if (strstr(path, PATH_NEEDLE)) {
    fuzzer_out_fd = ret;
    log_action("open64");
  }

	return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
  static ssize_t (*real_write)(int, const void *, size_t);
  if (!real_write)
    real_write = dlsym(RTLD_NEXT, "write");

  if (fd == fuzzer_out_fd) {
    zmq_send(sender, buf, count, 0);
  }

  return real_write(fd, buf, count);
}

__attribute__((constructor)) static void before_main(void)
{
  start_time_ms = gettimems();
  pid = getpid();

  set_log_file();
  log_action("open_log");

  context = zmq_ctx_new();
  sender = zmq_socket(context, ZMQ_PUSH);
  if (!sender) {
    log_action("fail_sender");
    exit(1);
  }
  if (zmq_connect(sender, "tcp://localhost:5558") == -1) {
    log_action("fail_connect");
    exit(1);
  }
  log_action("open_zmq");

  setenv("LD_PRELOAD", "", 1);
}

__attribute__((destructor)) static void after_main(void)
{
  if (pid == getpid()) {
    zmq_close(sender);
    zmq_ctx_destroy(context);
    log_action("close_zmq");
    fclose(log_file);
  }
}
