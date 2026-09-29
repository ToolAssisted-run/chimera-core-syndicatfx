#ifndef GAME_STATE_H
#define GAME_STATE_H
#include <stdint.h>
int gs_domain_count(void);
const char *gs_domain_name(int i);
uint8_t *gs_domain_ptr(int i);
int64_t gs_domain_size(int i);
int gs_domain_writable(int i);
const char *gs_properties_json(void);
void gamestate_from_game(int ended);   /* after every step */
void gamestate_to_game(void);          /* before every step */
#endif
