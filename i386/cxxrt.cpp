// cxxrt.cpp - the C++ runtime pieces bflibrary needs, for the freestanding i386 build on musl
#include <stdlib.h>
void *operator new(unsigned int n) { return malloc(n ? n : 1); }
void *operator new[](unsigned int n) { return malloc(n ? n : 1); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, unsigned int) noexcept { free(p); }
void operator delete[](void *p, unsigned int) noexcept { free(p); }
extern "C" {
void *__dso_handle = &__dso_handle;
void __cxa_pure_virtual(void) { abort(); }
}
