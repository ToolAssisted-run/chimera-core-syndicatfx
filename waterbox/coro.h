#ifndef CORO_H
#define CORO_H
#include <stddef.h>
typedef struct Coro {
    void *sp, *caller_sp;
    void *stack; size_t stack_size;
    void (*fn)(void *); void *arg;
    int done;
} Coro;
int coro_init(Coro *c, size_t stack_size, void (*fn)(void *), void *arg);
void coro_resume(Coro *c);      /* run until the coroutine yields or ends */
void coro_yield(Coro *c);       /* from inside: back to the resumer */
void coro_switch(void **save_sp, void *load_sp);
#endif
