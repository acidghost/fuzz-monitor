#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <zmq.h>
#include <unistd.h>


#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)


#if defined(AFL)
#   define PATH_NEEDLE "cur_input"
#elif defined(HONGG)
#   define PATH_NEEDLE "honggfuzz.input"
#else
#   error "no known fuzzer defined"
#endif

#define STR(s) #s
#define _LOG_FILENAME(x) STR(x) ".log"
#define LOG_FILENAME _LOG_FILENAME(FUZZ)
#define SKIP_N 100

static pid_t pid;
static int fuzzer_out_fd;
static FILE *log_file;
static long start_time_ms;
static void *context;
static void *sender;
static unsigned long counter = 0;

static inline long get_time_ms(void)
{
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return round(spec.tv_nsec / 1.0e6);
}

static inline void log_action(char *str)
{
    long ms = get_time_ms() - start_time_ms;
    fprintf(log_file, "%ld\t%s\n", ms, str);
}


#define MAKE_OPEN(open)                                                         \
int open(const char *path, int flags, ...)                                      \
{                                                                               \
    static int (*real_##open)(const char *, int, ...);                          \
    if (unlikely(!real_##open))                                                 \
        real_##open = dlsym(RTLD_NEXT, STR(open));                              \
                                                                                \
    va_list args;                                                               \
    va_start(args, flags);                                                      \
    int ret;                                                                    \
    if (__OPEN_NEEDS_MODE(flags)) {                                             \
        mode_t mode = va_arg(args, mode_t);                                     \
        ret = real_##open(path, flags, mode);                                   \
    } else {                                                                    \
        ret = real_##open(path, flags);                                         \
    }                                                                           \
    va_end(args);                                                               \
                                                                                \
    if (strstr(path, PATH_NEEDLE)) {                                            \
        fuzzer_out_fd = ret;                                                    \
    }                                                                           \
                                                                                \
    return ret;                                                                 \
}

MAKE_OPEN(open);
MAKE_OPEN(open64);

ssize_t write(int fd, const void *buf, size_t count)
{
    static ssize_t (*real_write)(int, const void *, size_t);
    if (unlikely(!real_write))
        real_write = dlsym(RTLD_NEXT, "write");

    if (fd == fuzzer_out_fd) {
        counter++;
        if (unlikely(counter > SKIP_N)) {
          zmq_send(sender, buf, count, 0);
          counter = 0;
        }
    }

    return real_write(fd, buf, count);
}

__attribute__((constructor)) static void before_main(void)
{
    start_time_ms = get_time_ms();
    pid = getpid();

    log_file = fopen(LOG_FILENAME, "w");
    if (!log_file) {
        fprintf(stderr, "Unable to open " LOG_FILENAME "\n");
        exit(EXIT_FAILURE);
    }
    log_action("open_log");

    context = zmq_ctx_new();
    sender = zmq_socket(context, ZMQ_PUSH);
    if (!sender) {
        log_action("fail_sender");
        exit(EXIT_FAILURE);
    }
    if (zmq_connect(sender, "tcp://localhost:5558") == -1) {
        log_action("fail_connect");
        exit(EXIT_FAILURE);
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
