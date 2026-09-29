#ifndef _INCLUDE_BFCONFIG_H
#define _INCLUDE_BFCONFIG_H 1
/* Define to 1 if the bflibrary should support MouseMoveRatio properties
   within lbDisplay */
/* Define to 1 if the bflibrary should maintain mouse wheel properties within
   lbDisplay */
#ifndef LB_ENABLE_SDL2
#define LB_ENABLE_SDL2 1
#endif
/* Define to 1 if the bflibrary should define and use lbDisplay.ShadowColour
   property */
#ifndef LB_HAVE_FCNTL_H
#define LB_HAVE_FCNTL_H 1
#endif
#ifndef LB_HAVE_GETCWD
#define LB_HAVE_GETCWD 1
#endif
#ifndef LB_HAVE_INTTYPES_H
#define LB_HAVE_INTTYPES_H 1
#endif
/* Define to 1 if your system has a GNU libc compatible `malloc' function, and
   to 0 otherwise. */
#ifndef LB_HAVE_MALLOC
#define LB_HAVE_MALLOC 1
#endif
#ifndef LB_HAVE_MEMMOVE
#define LB_HAVE_MEMMOVE 1
#endif
#ifndef LB_HAVE_MEMSET
#define LB_HAVE_MEMSET 1
#endif
#ifndef LB_HAVE_MKDIR
#define LB_HAVE_MKDIR 1
#endif
/* Define to 1 if your system has a GNU libc compatible `realloc' function,
   and to 0 otherwise. */
#ifndef LB_HAVE_REALLOC
#define LB_HAVE_REALLOC 1
#endif
#ifndef LB_HAVE_RMDIR
#define LB_HAVE_RMDIR 1
#endif
#ifndef LB_HAVE_STDINT_H
#define LB_HAVE_STDINT_H 1
#endif
#ifndef LB_HAVE_STDIO_H
#define LB_HAVE_STDIO_H 1
#endif
#ifndef LB_HAVE_STDLIB_H
#define LB_HAVE_STDLIB_H 1
#endif
#ifndef LB_HAVE_STRINGS_H
#define LB_HAVE_STRINGS_H 1
#endif
#ifndef LB_HAVE_STRING_H
#define LB_HAVE_STRING_H 1
#endif
#ifndef LB_HAVE_SYS_STAT_H
#define LB_HAVE_SYS_STAT_H 1
#endif
#ifndef LB_HAVE_SYS_TYPES_H
#define LB_HAVE_SYS_TYPES_H 1
#endif
#ifndef LB_HAVE_UNISTD_H
#define LB_HAVE_UNISTD_H 1
#endif
#ifndef LB_HAVE__BOOL
#define LB_HAVE__BOOL 1
#endif
#ifndef LB_PACKAGE
#define LB_PACKAGE "bflibrary"
#endif
#ifndef LB_PACKAGE_BUGREPORT
#define LB_PACKAGE_BUGREPORT "mefistotelis@gmail.com"
#endif
#ifndef LB_PACKAGE_NAME
#define LB_PACKAGE_NAME "bullfrog-library"
#endif
#ifndef LB_PACKAGE_STRING
#define LB_PACKAGE_STRING "bullfrog-library 0.1.2.0"
#endif
#ifndef LB_PACKAGE_TARNAME
#define LB_PACKAGE_TARNAME "bflibrary"
#endif
#ifndef LB_PACKAGE_URL
#define LB_PACKAGE_URL ""
#endif
#ifndef LB_PACKAGE_VERSION
#define LB_PACKAGE_VERSION "0.1.2.0"
#endif
/* Define to 1 if the bflibrary should log a warning on exceptionally long
   polygon render */
/* Define to 1 if all of the C90 standard headers exist (not just the ones
   required in a freestanding environment). This macro is provided for
   backward compatibility; new code need not use it. */
#ifndef LB_STDC_HEADERS
#define LB_STDC_HEADERS 1
#endif
#ifndef LB_VERSION
#define LB_VERSION "0.1.2.0"
#endif
/* Define for Solaris 2.5.1 so the uint32_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* Define for Solaris 2.5.1 so the uint64_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* Define for Solaris 2.5.1 so the uint8_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
#endif
/* Define to the type of a signed integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
/* Define to the type of a signed integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
/* Define to the type of a signed integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
/* Define to the type of a signed integer type of width exactly 8 bits if such
   a type exists and the standard includes do not define it. */
/* Define to the type of an unsigned integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
/* Define to the type of an unsigned integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
/* Define to the type of an unsigned integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
/* Define to the type of an unsigned integer type of width exactly 8 bits if
   such a type exists and the standard includes do not define it. */
#endif
