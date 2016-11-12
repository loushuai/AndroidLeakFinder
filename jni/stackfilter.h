#ifndef _STACK_FILTER_H
#define _STACK_FILTER_H

#include "backtrace.h"

#define FILTER_SIZE 16

void read_config(void);
int match_stack(backtrace_stack_t *stack);

#endif


