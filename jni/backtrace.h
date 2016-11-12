#ifndef _BACK_TRACE_H
#define _BACK_TRACE_H

typedef struct {
	void **ips;
	size_t n;
} backtrace_stack_t;

/*
 * Describes a single frame of a backtrace.
 */
typedef struct {
    uintptr_t absolute_pc;     /* absolute PC offset */
    uintptr_t stack_top;       /* top of stack for this frame */
    size_t stack_size;         /* size of this stack frame */
} backtrace_frame_t;

/*
 * Describes the symbols associated with a backtrace frame.
 */
typedef struct {
	const void* addr;       /* relative frame PC offset from the start of the library,
                                    or the absolute PC if the library is unknown */
    const char* map_name;              /* executable or library name, or NULL if unknown */
    const char* symbol_name;           /* symbol name, or NULL if unknown */
} backtrace_symbol_t;

/*
 * Unwinds the call stack for the current thread of execution.
 * Populates the backtrace array with the program counters from the call stack.
 * Returns the number of frames collected, or -1 if an error occurred.
 */
size_t backtrace(backtrace_stack_t *stack, size_t max_depth);
//size_t backtrace(backtrace_symbol_t *symbols, size_t count);

/*
 * Gets the symbols for each frame of a backtrace.
 * The symbols array must be big enough to hold one symbol record per frame.
 * The symbols must later be freed using free_backtrace_symbols.
 */
backtrace_symbol_t* getBacktraceSymbols(size_t count);

/*
 * Frees the storage associated with backtrace symbols.
 */
void freeBacktraceSymbols(backtrace_symbol_t* symbols);

enum {
    // A hint for how big to make the line buffer for format_backtrace_line
    MAX_BACKTRACE_LINE_LENGTH = 800,
};

enum {
	MAX_BACKTRACE_DEPTH = 32,
};

#endif
