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

#define STATIC_BUF_LEN 8192
static char static_buf[STATIC_BUF_LEN];
size_t static_buf_len;

static pthread_mutex_t mh_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t static_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_key_t thread_key;
static pthread_once_t init_done = PTHREAD_ONCE_INIT;

static void *(*mallocp)(size_t);
static void *(*callocp)(size_t, size_t);
static void *(*reallocp)(void *, size_t);
static void *(*memalignp)(size_t, size_t);
static int   (*posix_memalignp)(void **, size_t, size_t);
static void  (*freep)(void *);

static struct cds_hlist_head mh_table[TABLE_SIZE];
static size_t alloced_size;
static size_t freed_size;
static size_t alloced_n;

struct mh_entry {
  struct cds_hlist_node hlist;
  void *ptr;
  size_t alloc_size;
  backtrace_stack_t stack;
};

static struct mh_entry *get_mh(const void *ptr)
{
  struct cds_hlist_head *head;
  struct cds_hlist_node *node;
  struct mh_entry *e;
  uint32_t hash;

  hash = jhash(&ptr, sizeof(ptr), 0);
  head = &mh_table[hash & (TABLE_SIZE - 1)];
  cds_hlist_for_each_entry(e, node, head, hlist) {
    if (ptr == e->ptr)
      return e;
  }
  return NULL;
}

static void add_mh(void *ptr, size_t alloc_size, backtrace_stack_t *stack)
{
  struct cds_hlist_head *head;
  struct cds_hlist_node *node;
  struct mh_entry *e;
  uint32_t hash;
  Dl_info info;

  if (!ptr)
    return;

  hash = jhash(&ptr, sizeof(ptr), 0);
  head = &mh_table[hash & (TABLE_SIZE - 1)];
  cds_hlist_for_each_entry(e, node, head, hlist) {
    if (ptr == e->ptr) {
      return;
      //assert(0);	/* already there */
    }
  }

  e = (struct mh_entry *)mallocp(sizeof(*e));
  e->ptr = ptr;
  e->alloc_size = alloc_size;
  e->stack = *stack;

  cds_hlist_add_head(&e->hlist, head);

  //  alloced_size += alloc_size;
  //  ++alloced_n;
}

static void del_mh(void *ptr)
{
  struct mh_entry *e;

  if (!ptr)
    return;

  e = get_mh(ptr);
  if (!e) {
    return;
  }

  cds_hlist_del(&e->hlist);
  freep(e);

  freed_size += e->alloc_size;
  //  --alloced_n;
}

void thread_key_init(void)
{
  pthread_key_create(&thread_key, NULL);
}

static __attribute__((constructor))
void init(void)
{
  pthread_once(&init_done, thread_key_init);

  mallocp = (void *(*) (size_t)) dlsym (RTLD_NEXT, "malloc");
  callocp = (void *(*) (size_t, size_t)) dlsym (RTLD_NEXT, "calloc");
  reallocp = (void *(*) (void *, size_t)) dlsym (RTLD_NEXT, "realloc");
  memalignp = (void *(*)(size_t, size_t)) dlsym (RTLD_NEXT, "memalign");
  posix_memalignp = (int (*)(void **, size_t, size_t)) dlsym (RTLD_NEXT, "posix_memalign");
  freep = (void (*) (void *)) dlsym (RTLD_NEXT, "free");
}

static void *static_alloc(size_t n, size_t size)
{
  void *retptr = NULL;

  pthread_mutex_lock(&static_alloc_mutex);

  if (n * size > sizeof(static_buf) - static_buf_len) {
    pthread_mutex_unlock(&static_alloc_mutex);
    return NULL;
  }

  retptr = static_buf + static_buf_len;
  static_buf_len += n * size;

  pthread_mutex_unlock(&static_alloc_mutex);

  return retptr;
}

void *malloc(size_t size)
{
  void *result;

  if (mallocp == NULL) {
    init();
    result = static_alloc(1, size);
    return result;
  }

  result = mallocp(size);

  if (pthread_getspecific(thread_key)) {
    return result;
  }

  pthread_setspecific(thread_key, (void *)0x01);

  backtrace_stack_t stk;
  int n = backtrace(&stk, MAX_BACKTRACE_DEPTH);

  pthread_mutex_lock(&mh_mutex);
  if (n > 0) {
    add_mh(result, size, &stk);
  }
  pthread_mutex_unlock(&mh_mutex);

  pthread_setspecific(thread_key, NULL);

  return result;
}

void *calloc(size_t nmemb, size_t size)
{
  void *result;

  if (callocp == NULL) {
    init();
    result = static_alloc(nmemb, size);
    return result;
  }

  result = callocp(nmemb, size);

  if (pthread_getspecific(thread_key)) {
    return result;
  }

  pthread_setspecific(thread_key, (void *)0x01);

  backtrace_stack_t stk;
  int n = backtrace(&stk, MAX_BACKTRACE_DEPTH);

  pthread_mutex_lock(&mh_mutex);
  if (n > 0) {
    add_mh(result, nmemb * size, &stk);
  }
  pthread_mutex_unlock(&mh_mutex);

  pthread_setspecific(thread_key, NULL);

  return result;
}

void *realloc(void *ptr, size_t size)
{
  void *result;

  if (reallocp == NULL) {
    init();
    free(ptr);
    result = static_alloc(1, size);
    return result;
  }

  result = reallocp(ptr, size);

  if (pthread_getspecific(thread_key)) {
    return result;
  }

  pthread_setspecific(thread_key, (void *)0x01);

  backtrace_stack_t stk;
  int n = backtrace(&stk, MAX_BACKTRACE_DEPTH);

  pthread_mutex_lock(&mh_mutex);
  del_mh(ptr);

  if (size > 0 && n > 0) {
    add_mh(result, size, &stk);
  }
  pthread_mutex_unlock(&mh_mutex);

  pthread_setspecific(thread_key, NULL);

  return result;
}

void *memalign(size_t alignment, size_t size)
{
  void *result;

  if (memalignp == NULL) {
    init();
    result = static_alloc(1, size); //Fixme, not aligned
    return result;
  }

  result = memalignp(alignment, size);

  if (pthread_getspecific(thread_key)) {
    return result;
  }

  pthread_setspecific(thread_key, (void *)0x01);

  backtrace_stack_t stk;
  int n = backtrace(&stk, MAX_BACKTRACE_DEPTH);

  pthread_mutex_lock(&mh_mutex);
  if (n > 0) {
    add_mh(result, size, &stk);
  }
  pthread_mutex_unlock(&mh_mutex);

  pthread_setspecific(thread_key, NULL);

  return result;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
  int result;

  if (posix_memalignp == NULL) {
    init();
    *memptr = static_alloc(1, size); //Fixme: not aligned
    return 0;
  }

  result = posix_memalignp(memptr, alignment, size);

  if (pthread_getspecific(thread_key)) {
    return result;
  }

  pthread_setspecific(thread_key, (void *)0x01);

  backtrace_stack_t stk;
  int n = backtrace(&stk, MAX_BACKTRACE_DEPTH);

  pthread_mutex_lock(&mh_mutex);
  if (n > 0) {
    add_mh(*memptr, size, &stk);
  }
  pthread_mutex_unlock(&mh_mutex);

  pthread_setspecific(thread_key, NULL);

  return result;
}

void free(void *ptr)
{
  if (ptr >= (void*) static_buf && ptr < (void*)(static_buf + STATIC_BUF_LEN)) {
    return;
  } else {
    freep(ptr);
  }

  if (pthread_getspecific(thread_key)) {
    return;
  }

  pthread_mutex_lock(&mh_mutex);
  del_mh(ptr);
  pthread_mutex_unlock(&mh_mutex);
}

void print_leaks(void)
{
  size_t i = 0;
  int j = 0;
  size_t leak_size = 0;
  size_t leak_n = 0;
  char logbuf[256] = {0};

  pthread_setspecific(thread_key, (void *)0x01);

  pthread_mutex_lock(&mh_mutex);

  read_config();

  //  __android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder",
  //                        "%d leaks, total alloced: %d, total freed %d, in used %d",
  //                        alloced_n, alloced_size, freed_size, alloced_size - freed_size);

  for (i = 0; i < TABLE_SIZE; ++i) {
    struct cds_hlist_head *head;
    struct cds_hlist_node *node;
    struct mh_entry *e;

    head = &mh_table[i];

    cds_hlist_for_each_entry(e, node, head, hlist) {
      Dl_info info;
      char *caller_symbol = NULL;
      char *file_name = NULL;

      if (!match_stack(&(e->stack))) {
        continue;
      }

      ++leak_n;
      leak_size += e->alloc_size;

      __android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder",
                          "Leak %d, size %d", leak_n, e->alloc_size);

      // print stack
      for (int i = 1; i < e->stack.n; ++i) {
        dladdr(e->stack.ips[i], &info);
        if (info.dli_sname) {
          caller_symbol = strdup(info.dli_sname);
        }
        if (info.dli_fname) {
          file_name = strdup(info.dli_fname);
        }

        __android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder",
                            "#%d \t %s \t %s",
                            i-1, file_name == NULL ? "Unknown" : file_name,
                            caller_symbol == NULL ? "Unknown" : caller_symbol);

        if (caller_symbol) {
          freep(caller_symbol);
          caller_symbol = NULL;
        }
        if (file_name) {
          freep(file_name);
          file_name = NULL;
        }
      }

      __android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder", "\n");
    }
  }

  __android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder",
                      "=== Total leaks %d, leak size %d byte ===",
                      leak_n, leak_size);

  pthread_mutex_unlock(&mh_mutex);

  pthread_setspecific(thread_key, NULL);
}
