#include <stdio.h>
#include <string.h>
#include <unwind.h>
#include <dlfcn.h>
#include <android/log.h>
#include "backtrace.h"

typedef struct
{
    void** current;
    void** end;
} BacktraceState;

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg)
{
	BacktraceState* state = (BacktraceState *)arg;

	if (state->current < state->end) {
		void* ip = (void*)_Unwind_GetIP(context);
		if (ip) {
			*state->current = ip;
			state->current += 1;
			return _URC_NO_REASON;
		}
	}

	return _URC_END_OF_STACK;
}

static size_t captureBacktrace(void **buffer, size_t count)
{
    BacktraceState state = {buffer, buffer + count};

    _Unwind_Backtrace(unwindCallback, &state);

    return state.current - buffer;
}

size_t backtrace(backtrace_stack_t *stack, size_t max_depth)
{
	void *buffer[MAX_BACKTRACE_DEPTH] = {NULL};
	if (!stack) return 0;

	if (max_depth > MAX_BACKTRACE_DEPTH) {
		max_depth = MAX_BACKTRACE_DEPTH;
	}

	stack->n = captureBacktrace(buffer, max_depth);
	if (stack->n > 0) {
		stack->ips = (void **)malloc(sizeof(void *) * stack->n);
		if (!stack->ips) {
			return 0;
		}
	}

	for (int i = 0; i < stack->n; ++i) {
		stack->ips[i] = buffer[i];
	}

	return stack->n;
}

backtrace_symbol_t* getBacktraceSymbols(size_t count)
{
	return (backtrace_symbol_t*)calloc(count, sizeof(backtrace_symbol_t));
}

void freeBacktraceSymbols(backtrace_symbol_t* symbols)
{
	free(symbols);
}
