#include <stdio.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <android/log.h>
#include "hlist.h"
#include "jhash.h"
#include "backtrace.h"
#include "stackfilter.h"
#include "common.h"

static struct cds_hlist_head fd_table[TABLE_SIZE];

struct fd_entry {
    struct cds_hlist_node hlist;
    int fd;
    backtrace_stack_t stack;
};

static int (*openp)(const char *pathname, int flags, mode_t mode);

static struct fd_entry *get_fd(const int fd)
{
    struct cds_hlist_head *head;
    struct cds_hlist_node *node;
    struct fd_entry *e;
    uint32_t hash;

    hash = jhash(&fd, sizeof(fd), 0);
    head = &fd_table[hash & (TABLE_SIZE - 1)];
    cds_hlist_for_each_entry(e, node, head, hlist) {
        if (fd == e->fd)
            return e;
    }
    return NULL;
}

static void add_fd(int fd, backtrace_stack_t *stack)
{
    struct cds_hlist_head *head;
    struct cds_hlist_node *node;
    struct fd_entry *e;
    uint32_t hash;
    Dl_info info;

    if (fd < 0)
        return;

    hash = jhash(&fd, sizeof(fd), 0);
    head = &fd_table[hash & (TABLE_SIZE - 1)];
    cds_hlist_for_each_entry(e, node, head, hlist) {
        if (fd == e->fd) {
            return;
        }
    }

    e = (struct fd_entry *)malloc(sizeof(*e));
    e->fd = fd;
    e->stack = *stack;

    cds_hlist_add_head(&e->hlist, head);
}

static __attribute__((constructor))
void init(void)
{
    openp = (int (*) (const char *, int, mode_t)) dlsym(RTLD_NEXT, "open");
}

int open(const char *pathname, int flags, ...)
{
    int result;
    va_list ap;
    mode_t mode;

    if (openp == NULL) {
        init();
    }

    va_start(ap, flags);

    mode = (mode_t)va_arg(ap, int);

    backtrace_stack_t stk;
    int n = backtrace(&stk, MAX_BACKTRACE_DEPTH);

    result = openp(pathname, flags, mode);
    if (result >= 0) {
        add_fd(result, &stk);
    }

    __android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder", "open %d", result);

    va_end(ap);
    return result;
}
