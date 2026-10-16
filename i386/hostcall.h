/* hostcall.h - services the translated program asks of the core, as Linux i386 syscalls with numbers
 * no kernel uses. Shared by the i386 side (sdlshim.c) and the core (waterbox/). */
#ifndef HOSTCALL_H
#define HOSTCALL_H

#define HC_STEP     0x5C00  /* end of step: the game reads input now. arg1 = guest HcInput* (filled by the host) */
#define HC_PRESENT  0x5C01  /* arg1 = pixels (8-bit), arg2 = width, arg3 = height, arg4 = pitch, arg5 = palette (256 x RGBA) */
#define HC_TURN     0x5C02  /* game loop head: arg1 = level block address, arg2 = length (verification dumps) */
#define HC_LOG      0x5C03  /* arg1 = text, arg2 = length */
#define HC_TICKS    0x5C04  /* returns the virtual clock in milliseconds */
#define HC_DELAY    0x5C05  /* arg1 = milliseconds: the virtual clock does not advance (the step does); ignored */
#define HC_AUDIO_FRAMES 0x5C06  /* the step is over: fixes its length, returns its sound frames (44100 Hz stereo) */
#define HC_AUDIO    0x5C07  /* arg1 = the step's sound (frames x 2 int16), arg2 = frames */
#define HC_CODE     0x5C08  /* arg1 = address, arg2 = length: code loaded at run time, for the interpreter */
#define HC_PIT      0x5C09  /* arg1 = the PIT's channel 0 divisor (0 = 65536): its interrupt's period */
#define HC_PIT_TICK 0x5C0A  /* 1: a PIT interrupt is due - run it now (it happens at its own time); 0: none */

#define HC_KEYS 256
typedef struct HcInput {
    unsigned char keys[HC_KEYS];   /* SDL scancodes held this step */
    int mouse_x, mouse_y;          /* pointer position in pixels of the last presented picture */
    int buttons;                   /* bit 0 left, bit 1 right, bit 2 middle */
    int quit;
} HcInput;

#ifndef HOSTCALL_NO_I386
static inline long hc_call(long nr, long a1, long a2, long a3, long a4, long a5)
{
    long r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(nr), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "memory");
    return r;
}
#endif
#endif
