#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
int main(int argc, char **argv)
{
    int v[8] = {5, 3, 9, 1, 7, 2, 8, 6}; char buf[64];
    qsort(v, 8, sizeof v[0], cmp);
    snprintf(buf, sizeof buf, "%d %d %d %s %lld", v[0], v[7], fib(20), argc > 1 ? argv[1] : "-", (long long)v[3] * 1000000007LL);
    char *p = malloc(100); strcpy(p, buf); printf("hello: %s (%u)\n", p, (unsigned)strlen(p));
    unsigned long long q = 0x123456789abcdefULL; printf("div %llu %llu\n", q / 12345, q % 12345);
    FILE *f = fopen("hello.txt", "r"); if (f) { fgets(buf, sizeof buf, f); printf("file: %s", buf); fclose(f); }
    free(p); return 3;
}
