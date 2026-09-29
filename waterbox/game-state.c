/* game-state.c - Syndicate's memory and properties (chimera docs/game-cores.md).
 *
 * The translated program keeps the original's data layout inside its 32-bit arena, so the game's
 * memory is exposed in place: "Level" is the level block the original loads each mission from
 * GAMExx.DAT and runs every turn on (0x80108..0x9C632 in the DOS executable: the random seed, the map
 * of who stands where, the people, vehicles, objects, weapons, effects, commands and objectives), and
 * "Arena" is the whole address space of the translated program. The Game State block holds what is
 * the core's: the steps and turns run, the mission, whether the program ended. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "game-state.h"
#include "sfx-machine.h"
#include "xl_image.h"

#define LEVEL_LEN (0x9C632u - 0x80108u)

#pragma pack(push, 1)
typedef struct {
    uint32_t steps;      /* +0 steps run (a step is one pass of the game's loop, 1/16 s) */
    uint32_t turns;      /* +4 turns of a mission's game loop */
    uint16_t mission;    /* +8 the mission being played (current_levno) */
    uint8_t ended;       /* +10 the program ended */
} game_state;
#pragma pack(pop)
static game_state g_state;

void gamestate_from_game(int ended)
{
    uint8_t *a = sfx_arena();
    g_state.steps = (uint32_t)sfx_steps();
    g_state.turns = (uint32_t)sfx_turns();
    if (a) memcpy(&g_state.mission, a + XLSYM_current_levno, 2);
    g_state.ended = (uint8_t)(ended != 0);
}
void gamestate_to_game(void) { }   /* nothing in the block is the game's to be told */

typedef struct { const char *name; int64_t size; int writable; } domain;
static const domain g_domains[] = {{"Game State", sizeof(game_state), 0}, {"Level", LEVEL_LEN, 1}, {"Arena", 0, 1}};
#define NDOMAINS 3
int gs_domain_count(void) { return NDOMAINS; }
const char *gs_domain_name(int i) { return i >= 0 && i < NDOMAINS ? g_domains[i].name : ""; }
uint8_t *gs_domain_ptr(int i)
{
    uint8_t *a = sfx_arena();
    switch (i) {
    case 0: return (uint8_t *)&g_state;
    case 1: return a ? a + XLSYM_level__Seed : NULL;
    case 2: return a;
    default: return NULL;
    }
}
int64_t gs_domain_size(int i) { return i == 2 ? (int64_t)sfx_arena_size() : i >= 0 && i < NDOMAINS ? g_domains[i].size : 0; }
int gs_domain_writable(int i) { return i >= 0 && i < NDOMAINS && g_domains[i].writable; }

/* ------------------------------------------------------------------ the table */
static char g_table[16 * 1024];
static int g_len, g_first = 1;
static void add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void add(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(g_table + g_len, sizeof g_table - (size_t)g_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_len += n;
}
static void prop(const char *name, const char *dom, long off, const char *type, const char *group, const char *extra)
{
    add("%s\n    { \"name\": \"%s\", \"domain\": \"%s\", \"offset\": %ld, \"type\": \"%s\", \"group\": \"%s\"%s%s }",
        g_first ? "" : ",", name, dom, off, type, group, extra[0] ? ", " : "", extra);
    g_first = 0;
}
const char *gs_properties_json(void)
{
    if (g_len) return g_table;
    add("{ \"properties\": [");
    prop("Steps", "Game State", 0, "u32", "Game", "\"writable\": false, \"description\": \"Steps run: one pass of the game's loop each, 1/16 s\"");
    prop("Turns", "Game State", 4, "u32", "Game", "\"writable\": false, \"description\": \"Turns of a mission's game loop run so far\"");
    prop("Mission", "Game State", 8, "u16", "Game", "\"writable\": false, \"description\": \"The mission being played (the game's level number)\"");
    prop("Ended", "Game State", 10, "bool", "Game", "\"writable\": false, \"description\": \"The program ended\"");
    /* the level block, in place (offsets from 0x80108 in the original) */
    prop("Level.Seed", "Level", 0, "u16", "Level", "\"description\": \"The random seed: seed*0x24A1+0x24DF each draw; loaded with the mission from GAMExx.DAT\"");
    prop("Level.Timer", "Level", 4, "u16", "Level", "\"description\": \"The level's timer word (0x8010C)\"");
    add("\n] }");
    return g_table;
}
