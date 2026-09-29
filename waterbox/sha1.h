#ifndef SHA1_H
#define SHA1_H
#include <stddef.h>
#include <stdint.h>
void sha1_hex(const void *data, size_t len, char out[41]);   /* uppercase hex */
#endif
