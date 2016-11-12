#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <android/log.h>
#include "stackfilter.h"

static const char* config_file = "/data/local/tmp/memleakconfig.txt";
static char *caller_filter[FILTER_SIZE];

void read_config(void)
{
  FILE *fp = NULL;
  char line_buf[256] = {0};
  int i = 0;

  // clear the filter
  for (i = 0; i < FILTER_SIZE; ++i) {
    if (caller_filter[i]) {
      free(caller_filter[i]);
      caller_filter[i] = NULL;
    }
  }

  if ((fp = fopen(config_file, "r")) == NULL) {
    __android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder",
                        "Open config file %s failed", config_file);
    return;
  }

  i = 0;
  while (!feof(fp)) {
    line_buf[0] = '\0';
    fscanf(fp, "%s", line_buf);
    // debug
    __android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder",
                        "read confige line %s, len %d", line_buf, strlen(line_buf));

    if (strlen(line_buf) > 0) {
      caller_filter[i++] = strdup(line_buf);
    }
    if (i == FILTER_SIZE) {
      break;
    }
  }

  fclose(fp);
}

int match_stack(backtrace_stack_t *stack)
{
  Dl_info info;
  char *file_name = NULL;
  if (!stack) return 0;

  for (int i = 0; i < stack->n; ++i) {
    dladdr(stack->ips[i], &info);
    for (int j = 0; j < FILTER_SIZE; ++j) {
      if (caller_filter[j] && info.dli_fname && strstr(info.dli_fname, caller_filter[j])) {
        return 1;
      }
    }
  }

  return 0;
}
