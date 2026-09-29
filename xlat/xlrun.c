/* xlrun.c - native test runner for translated i386 programs (not part of the core).
 * Loads the image into an arena, builds the process stack, and passes Linux i386 syscalls through
 * to the host OS (pointers translated). usage: xlrun [args...]
 * Built with the translation and the runtime: xlat_rt.c, xl_interp.c. */
#define _GNU_SOURCE
#include "xlat.h"
#include "xl_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <time.h>

extern uint32_t xl_tls_base[8];
static jmp_buf exit_jb; static int exit_code;
static uint32_t brk_start, brk_cur, mmap_next;
#define STACK_TOP 0x0FFF0000u
#define MMAP_BASE 0x08000000u

void xl_host_fatal(const char *msg) { fprintf(stderr, "xlrun: fatal: %s\n", msg); exit(99); }
uint32_t xl_host_in(uint32_t port, int sz) { (void)port; (void)sz; return 0xFF; }
void xl_host_out(uint32_t port, uint32_t v, int sz) { (void)port; (void)v; (void)sz; }
static void *G(uint32_t a) { return xl_mem + (a & XL_MASK); }
static int32_t err(void) { return -errno; }

void xl_host_syscall(void)
{
    uint32_t nr = EAX, a1 = EBX, a2 = ECX, a3 = EDX, a4 = ESI, a5 = EDI, a6 = EBP; int32_t r = -38;  /* ENOSYS */
    (void)a6;
    switch (nr) {
    case 1: case 252: exit_code = (int)a1; longjmp(exit_jb, 1);
    case 3: r = read(a1, G(a2), a3); if (r < 0) r = err(); break;
    case 4: r = write(a1, G(a2), a3); if (r < 0) r = err(); break;
    case 5: r = open((char *)G(a1), a2, a3); if (r < 0) r = err(); break;
    case 6: r = close(a1); if (r < 0) r = err(); break;
    case 45: /* brk */
        if (a1 >= brk_start && a1 < MMAP_BASE) { if (a1 > brk_cur) memset(G(brk_cur), 0, a1 - brk_cur); brk_cur = a1; }
        r = brk_cur; break;
    case 54: r = -25; break;                               /* ioctl: ENOTTY */
    case 91: r = 0; break;                                 /* munmap: keep the pages */
    case 140: { off_t o = lseek(a1, ((off_t)a2 << 32) | a3, a5); if (o < 0) r = err(); else { int64_t v = o; memcpy(G(a4), &v, 8); r = 0; } break; }
    case 19: { off_t o = lseek(a1, (int32_t)a2, a3); r = o < 0 ? err() : (int32_t)o; break; }
    case 145: case 146: { /* readv/writev */
        int n = (int)a3; ssize_t tot = 0;
        for (int k = 0; k < n; k++) {
            uint32_t base = rd32(a2 + 8 * k), len = rd32(a2 + 8 * k + 4);
            if (!len) continue;
            ssize_t got = nr == 146 ? write(a1, G(base), len) : read(a1, G(base), len);
            if (got < 0) { if (!tot) tot = err(); break; }
            tot += got; if ((uint32_t)got < len) break;
        }
        r = (int32_t)tot; break; }
    case 175: case 174: r = 0; break;                      /* rt_sigprocmask/rt_sigaction */
    case 192: { /* mmap2: anonymous only, bump allocator */
        uint32_t len = (a2 + 0xFFF) & ~0xFFFu;
        if (!(a4 & 0x20)) { r = -19; break; }              /* ENODEV for file mappings */
        r = mmap_next; memset(G(mmap_next), 0, len); mmap_next += len; break; }
    case 125: case 219: r = 0; break;                      /* mprotect, madvise */
    case 243: { /* set_thread_area(struct user_desc *) */
        uint32_t entry = rd32(a1), base = rd32(a1 + 4);
        if (entry == 0xFFFFFFFFu) { entry = 6; wr32(a1, entry); }
        xl_tls_base[entry & 7] = base; r = 0; break; }
    case 258: r = 1000; break;                             /* set_tid_address: our tid */
    case 20: case 224: r = 1000; break;                    /* getpid, gettid */
    case 403: case 265: { /* clock_gettime(64): a fixed clock for tests */
        if (nr == 403) { int64_t s = 1500000000; wr64(a2, (uint64_t)s); wr64(a2 + 8, 0); }
        else { wr32(a2, 1500000000u); wr32(a2 + 4, 0); }
        r = 0; break; }
    case 197: case 195: case 196: { /* fstat64/stat64/lstat64 */
        struct stat st; int rr = nr == 197 ? fstat(a1, &st) : stat((char *)G(a1), &st);
        if (rr < 0) { r = err(); break; }
        memset(G(a2), 0, 96); wr32(a2 + 16, st.st_mode); wr64(a2 + 44, (uint64_t)st.st_size); wr32(a2 + 88, 0); r = 0; break; }
    default: fprintf(stderr, "xlrun: syscall %u unimplemented\n", nr); r = -38;
    }
    EAX = (uint32_t)r;
}

int main(int argc, char **argv)
{
    xl_mem = mmap(NULL, XL_ARENA_SIZE + 64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    uint32_t end = 0;
    for (uint32_t k = 0; k < xl_segment_count; k++) {
        const XlSegment *s = &xl_segments[k];
        memcpy(G(s->vaddr), s->data, s->filesz);
        if (s->vaddr + s->memsz > end) end = s->vaddr + s->memsz;
    }
    brk_start = brk_cur = (end + 0xFFF) & ~0xFFFu; mmap_next = MMAP_BASE;
    /* process stack: strings, then argc/argv/envp/auxv */
    uint32_t sp = STACK_TOP, argp[64]; int n = 0;
    const char *env[] = {"HOME=/", "PATH=/", NULL};
    uint32_t envp[8]; int ne = 0;
    for (int k = argc - 1; k >= 0; k--) { size_t l = strlen(argv[k]) + 1; sp -= l; memcpy(G(sp), argv[k], l); argp[k] = sp; n++; }
    argp[0] = argp[0];
    for (int k = 0; env[k]; k++) { size_t l = strlen(env[k]) + 1; sp -= l; memcpy(G(sp), env[k], l); envp[ne++] = sp; }
    sp -= 16; uint32_t rnd = sp; for (int k = 0; k < 16; k++) wr8(rnd + k, 0x5A ^ k);
    sp &= ~15u;
    uint32_t aux[][2] = {{3, xl_elf_phdr}, {4, xl_elf_phent}, {5, xl_elf_phnum}, {6, 4096}, {9, xl_elf_entry},
                         {11, 1000}, {12, 1000}, {13, 1000}, {14, 1000}, {23, 0}, {25, rnd}, {0, 0}};
    int naux = sizeof aux / sizeof aux[0];
    uint32_t words = 1 + argc + 1 + ne + 1 + 2 * naux;
    sp -= words * 4; sp &= ~15u;
    uint32_t p = sp;
    wr32(p, argc); p += 4;
    for (int k = 0; k < argc; k++) { wr32(p, argp[k]); p += 4; } wr32(p, 0); p += 4;
    for (int k = 0; k < ne; k++) { wr32(p, envp[k]); p += 4; } wr32(p, 0); p += 4;
    for (int k = 0; k < naux; k++) { wr32(p, aux[k][0]); wr32(p + 4, aux[k][1]); p += 8; }
    ESP = sp; xl.fcw = 0x37F;
    if (setjmp(exit_jb) == 0) { xl.eip = xl_elf_entry; xl_call(xl_elf_entry); fprintf(stderr, "xlrun: entry returned\n"); return 98; }
    fprintf(stderr, "xlrun: exit %d after %llu instructions\n", exit_code, (unsigned long long)xl.icount);
    return exit_code;
}
