/* corehooks.c - the i386 side of the hooks patches/0002 adds to the game */
#include "hostcall.h"
extern unsigned short level__Seed;       /* the level block: 0x80108..0x9C632 in the original */
void core_game_turn(void)
{
    hc_call(HC_TURN, (long)&level__Seed, 0x9C632 - 0x80108, 0, 0, 0);
}
