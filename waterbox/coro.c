/* coro.c - a minimal coroutine for x86-64 SysV (native and miniBox guest): the translated program runs
 * on its own stack and gives control back at every step boundary. The stack is mmap'd with MAP_STACK
 * (miniBox on Windows can only deliver faults on stacks it was told about). */
#include "coro.h"
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>

__asm__(
    ".text\n"
    ".globl coro_switch\n"
    ".type coro_switch,@function\n"
    "coro_switch:\n"
    "  push %rbp\n  push %rbx\n  push %r12\n  push %r13\n  push %r14\n  push %r15\n"
    "  sub $8, %rsp\n  stmxcsr (%rsp)\n  fnstcw 4(%rsp)\n"
    "  mov %rsp, (%rdi)\n"
    "  mov %rsi, %rsp\n"
    "  ldmxcsr (%rsp)\n  fldcw 4(%rsp)\n  add $8, %rsp\n"
    "  pop %r15\n  pop %r14\n  pop %r13\n  pop %r12\n  pop %rbx\n  pop %rbp\n"
    "  ret\n"
    ".size coro_switch, .-coro_switch\n");

static Coro *starting;
static void coro_trampoline(void)
{
    Coro *c = starting;
    c->fn(c->arg);
    c->done = 1;
    for (;;) coro_switch(&c->sp, c->caller_sp);   /* a finished coroutine only ever yields */
}

int coro_init(Coro *c, size_t stack_size, void (*fn)(void *), void *arg)
{
    memset(c, 0, sizeof *c);
    c->stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (c->stack == MAP_FAILED) { c->stack = NULL; return -1; }
    c->stack_size = stack_size; c->fn = fn; c->arg = arg;
    uint64_t *sp = (uint64_t *)((char *)c->stack + stack_size);
    sp = (uint64_t *)((uintptr_t)sp & ~(uintptr_t)15);
    *--sp = 0;                                   /* alignment: the trampoline starts as if called */
    *--sp = (uint64_t)(uintptr_t)coro_trampoline;
    for (int k = 0; k < 6; k++) *--sp = 0;      /* rbp rbx r12..r15 */
    *--sp = 0x0000037F00001F80ull;               /* mxcsr 0x1F80, x87 control word 0x037F */
    c->sp = sp;
    return 0;
}

void coro_resume(Coro *c)
{
    if (c->done) return;
    starting = c;
    coro_switch(&c->caller_sp, c->sp);
}

void coro_yield(Coro *c) { coro_switch(&c->sp, c->caller_sp); }
