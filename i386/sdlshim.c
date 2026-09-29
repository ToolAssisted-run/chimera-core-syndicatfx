/* sdlshim.c - the SDL2 subset bflibrary's SDL2 backend and the game use, implemented for the core.
 *
 * Compiled into the i386 program against SDL2's own headers, so the upstream backend builds unchanged.
 * The window surface is 8-bit, at the size of the current game mode; presenting it hands its pixels
 * and palette to the core. The first SDL_PollEvent of an event loop ends the step: the core returns
 * the input state and the shim turns the changes into SDL events. Time is the core's virtual clock. */
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdlib.h>
#include <string.h>
#include "hostcall.h"

typedef struct ShimSurf {
    SDL_Surface s;
    SDL_PixelFormat fmt;
    SDL_Palette pal;
    SDL_Color colors[256];
    int ck_on; Uint32 ck;
    int owns_pixels;
} ShimSurf;

struct SDL_Window { int w, h, flags; ShimSurf *surf; };
static SDL_Window the_window;

/* ---------------------------------------------------------------- basics */
int SDL_Init(Uint32 flags) { (void)flags; return 0; }
int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
void SDL_Quit(void) { }
const char *SDL_GetError(void) { return "sdlshim"; }
SDL_bool SDL_GetHintBoolean(const char *name, SDL_bool def) { (void)name; return def; }
SDL_bool SDL_SetHint(const char *name, const char *value) { (void)name; (void)value; return SDL_TRUE; }
int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
void SDL_SetWindowTitle(SDL_Window *w, const char *t) { (void)w; (void)t; }
void SDL_SetWindowIcon(SDL_Window *w, SDL_Surface *s) { (void)w; (void)s; }
SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc) { (void)src; (void)freesrc; return NULL; }
SDL_RWops *SDL_RWFromFile(const char *file, const char *mode) { (void)file; (void)mode; return NULL; }
int SDL_GetDesktopDisplayMode(int idx, SDL_DisplayMode *m) { (void)idx; m->format = SDL_PIXELFORMAT_INDEX8; m->w = 640; m->h = 480; m->refresh_rate = 60; m->driverdata = NULL; return 0; }
SDL_DisplayMode *SDL_GetClosestDisplayMode(int idx, const SDL_DisplayMode *mode, SDL_DisplayMode *closest) { (void)idx; *closest = *mode; return closest; }
int SDL_GetWindowDisplayIndex(SDL_Window *w) { (void)w; return 0; }
SDL_bool SDL_GetWindowWMInfo(SDL_Window *w, SDL_SysWMinfo *info) { (void)w; (void)info; return SDL_FALSE; }
Uint32 SDL_GetTicks(void) { return (Uint32)hc_call(HC_TICKS, 0, 0, 0, 0, 0); }
Uint64 SDL_GetTicks64(void) { return (Uint64)SDL_GetTicks(); }
void SDL_Delay(Uint32 ms) { hc_call(HC_DELAY, (long)ms, 0, 0, 0, 0); }

/* ---------------------------------------------------------------- surfaces */
static void set_masks(SDL_PixelFormat *f, int depth)
{
    memset(f, 0, sizeof *f);
    f->BitsPerPixel = (Uint8)depth; f->BytesPerPixel = (Uint8)((depth + 7) / 8);
    if (depth == 8) { f->format = SDL_PIXELFORMAT_INDEX8; return; }
    if (depth == 16) { f->format = SDL_PIXELFORMAT_RGB565; f->Rmask = 0xF800; f->Gmask = 0x07E0; f->Bmask = 0x001F; f->Rshift = 11; f->Gshift = 5; f->Rloss = 3; f->Gloss = 2; f->Bloss = 3; return; }
    if (depth == 24) f->format = SDL_PIXELFORMAT_RGB24; else f->format = SDL_PIXELFORMAT_RGB888;
    f->Rmask = 0xFF0000; f->Gmask = 0x00FF00; f->Bmask = 0x0000FF; f->Rshift = 16; f->Gshift = 8; f->Bshift = 0;
}
static ShimSurf *new_surf(int w, int h, int depth, void *pixels, int pitch)
{
    ShimSurf *ss = calloc(1, sizeof *ss);
    if (!ss) return NULL;
    set_masks(&ss->fmt, depth);
    if (depth == 8) { ss->pal.ncolors = 256; ss->pal.colors = ss->colors; ss->pal.refcount = 1; ss->fmt.palette = &ss->pal; }
    ss->s.format = &ss->fmt; ss->s.w = w; ss->s.h = h;
    ss->s.pitch = pitch ? pitch : ((w * ss->fmt.BytesPerPixel + 3) & ~3);
    if (pixels) ss->s.pixels = pixels;
    else { ss->s.pixels = calloc((size_t)ss->s.pitch * (h ? h : 1), 1); ss->owns_pixels = 1; }
    ss->s.clip_rect.w = w; ss->s.clip_rect.h = h; ss->s.refcount = 1;
    return ss;
}
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth, Uint32 r, Uint32 g, Uint32 b, Uint32 a)
{ (void)flags; (void)r; (void)g; (void)b; (void)a; ShimSurf *s = new_surf(w, h, depth, NULL, 0); return s ? &s->s : NULL; }
SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int w, int h, int depth, int pitch, Uint32 r, Uint32 g, Uint32 b, Uint32 a)
{ (void)r; (void)g; (void)b; (void)a; ShimSurf *s = new_surf(w, h, depth, pixels, pitch); return s ? &s->s : NULL; }
SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h, int depth, Uint32 format)
{ (void)flags; (void)format; ShimSurf *s = new_surf(w, h, depth, NULL, 0); return s ? &s->s : NULL; }
void SDL_FreeSurface(SDL_Surface *s)
{
    ShimSurf *ss = (ShimSurf *)s;
    if (!ss || ss == the_window.surf) return;
    if (ss->owns_pixels) free(ss->s.pixels);
    free(ss);
}
int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }
int SDL_SetPaletteColors(SDL_Palette *p, const SDL_Color *c, int first, int n)
{
    if (!p || first < 0 || first + n > p->ncolors) return -1;
    memcpy(p->colors + first, c, (size_t)n * sizeof *c); return 0;
}
int SDL_SetSurfacePalette(SDL_Surface *s, SDL_Palette *p)
{ if (s->format->palette && p) memcpy(s->format->palette->colors, p->colors, 256 * sizeof(SDL_Color)); return 0; }
int SDL_SetColorKey(SDL_Surface *s, int flag, Uint32 key) { ShimSurf *ss = (ShimSurf *)s; ss->ck_on = flag != 0; ss->ck = key; return 0; }
SDL_bool SDL_HasColorKey(SDL_Surface *s) { return ((ShimSurf *)s)->ck_on ? SDL_TRUE : SDL_FALSE; }
int SDL_GetColorKey(SDL_Surface *s, Uint32 *key) { ShimSurf *ss = (ShimSurf *)s; if (!ss->ck_on) return -1; *key = ss->ck; return 0; }
SDL_bool SDL_SetClipRect(SDL_Surface *s, const SDL_Rect *r)
{
    SDL_Rect full = {0, 0, s->w, s->h};
    if (!r) { s->clip_rect = full; return SDL_TRUE; }
    int x0 = r->x < 0 ? 0 : r->x, y0 = r->y < 0 ? 0 : r->y;
    int x1 = r->x + r->w > s->w ? s->w : r->x + r->w, y1 = r->y + r->h > s->h ? s->h : r->y + r->h;
    s->clip_rect.x = x0; s->clip_rect.y = y0; s->clip_rect.w = x1 > x0 ? x1 - x0 : 0; s->clip_rect.h = y1 > y0 ? y1 - y0 : 0;
    return s->clip_rect.w && s->clip_rect.h ? SDL_TRUE : SDL_FALSE;
}
void SDL_GetClipRect(SDL_Surface *s, SDL_Rect *r) { *r = s->clip_rect; }
Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b)
{
    if (f->BitsPerPixel == 8) {
        int best = 0; long bd = 0x7FFFFFFF;
        for (int i = 0; i < 256; i++) {
            const SDL_Color *c = &f->palette->colors[i];
            long d = (long)(c->r - r) * (c->r - r) + (long)(c->g - g) * (c->g - g) + (long)(c->b - b) * (c->b - b);
            if (d < bd) { bd = d; best = i; if (!d) break; }
        }
        return (Uint32)best;
    }
    return ((Uint32)r << f->Rshift) | ((Uint32)g << f->Gshift) | ((Uint32)b << f->Bshift);
}
static void put_px(SDL_Surface *d, int x, int y, Uint8 idx, const SDL_Color *pal)
{
    Uint8 *p = (Uint8 *)d->pixels + y * d->pitch + x * d->format->BytesPerPixel;
    if (d->format->BytesPerPixel == 1) { *p = idx; return; }
    SDL_Color c = pal[idx]; Uint32 v = ((Uint32)c.r << d->format->Rshift) | ((Uint32)c.g << d->format->Gshift) | ((Uint32)c.b << d->format->Bshift);
    if (d->format->BytesPerPixel == 4) memcpy(p, &v, 4);
    else if (d->format->BytesPerPixel == 3) { p[0] = (Uint8)v; p[1] = (Uint8)(v >> 8); p[2] = (Uint8)(v >> 16); }
    else { Uint16 w = (Uint16)v; memcpy(p, &w, 2); }
}
/* 8-bit source only (all this game blits); nearest-neighbour when scaled */
static int blit(SDL_Surface *src, const SDL_Rect *sr_in, SDL_Surface *dst, SDL_Rect *dr_in, int scaled)
{
    ShimSurf *ss = (ShimSurf *)src;
    SDL_Rect sr = sr_in ? *sr_in : (SDL_Rect){0, 0, src->w, src->h};
    SDL_Rect dr;
    if (scaled) dr = dr_in ? *dr_in : (SDL_Rect){0, 0, dst->w, dst->h};
    else { dr.x = dr_in ? dr_in->x : 0; dr.y = dr_in ? dr_in->y : 0; dr.w = sr.w; dr.h = sr.h; }
    if (src->format->BytesPerPixel != 1 || sr.w <= 0 || sr.h <= 0 || dr.w <= 0 || dr.h <= 0) return 0;
    const SDL_Color *pal = src->format->palette ? src->format->palette->colors : NULL;
    SDL_Rect c = dst->clip_rect;
    for (int y = 0; y < dr.h; y++) {
        int dy = dr.y + y; if (dy < c.y || dy >= c.y + c.h) continue;
        int sy = sr.y + (scaled ? y * sr.h / dr.h : y); if (sy < 0 || sy >= src->h) continue;
        const Uint8 *row = (const Uint8 *)src->pixels + sy * src->pitch;
        for (int x = 0; x < dr.w; x++) {
            int dx = dr.x + x; if (dx < c.x || dx >= c.x + c.w) continue;
            int sx = sr.x + (scaled ? x * sr.w / dr.w : x); if (sx < 0 || sx >= src->w) continue;
            Uint8 v = row[sx];
            if (ss->ck_on && v == (Uint8)ss->ck) continue;
            put_px(dst, dx, dy, v, pal);
        }
    }
    if (dr_in && !scaled) { dr_in->w = sr.w; dr_in->h = sr.h; }
    return 0;
}
int SDL_UpperBlit(SDL_Surface *s, const SDL_Rect *sr, SDL_Surface *d, SDL_Rect *dr) { return blit(s, sr, d, dr, 0); }
int SDL_LowerBlit(SDL_Surface *s, SDL_Rect *sr, SDL_Surface *d, SDL_Rect *dr) { return blit(s, sr, d, dr, 0); }
int SDL_UpperBlitScaled(SDL_Surface *s, const SDL_Rect *sr, SDL_Surface *d, SDL_Rect *dr) { return blit(s, sr, d, dr, 1); }
int SDL_LowerBlitScaled(SDL_Surface *s, SDL_Rect *sr, SDL_Surface *d, SDL_Rect *dr) { return blit(s, sr, d, dr, 1); }
int SDL_FillRect(SDL_Surface *d, const SDL_Rect *r, Uint32 color)
{
    SDL_Rect a = r ? *r : (SDL_Rect){0, 0, d->w, d->h}, c = d->clip_rect;
    int x0 = a.x > c.x ? a.x : c.x, y0 = a.y > c.y ? a.y : c.y;
    int x1 = a.x + a.w < c.x + c.w ? a.x + a.w : c.x + c.w, y1 = a.y + a.h < c.y + c.h ? a.y + a.h : c.y + c.h;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        Uint8 *p = (Uint8 *)d->pixels + y * d->pitch + x * d->format->BytesPerPixel;
        if (d->format->BytesPerPixel == 1) *p = (Uint8)color; else memcpy(p, &color, d->format->BytesPerPixel);
    }
    return 0;
}

/* ---------------------------------------------------------------- window */
static void window_resize(int w, int h)
{
    if (the_window.surf && the_window.surf->s.w == w && the_window.surf->s.h == h) return;
    ShimSurf *old = the_window.surf;
    the_window.surf = new_surf(w, h, 8, NULL, w);
    if (old) { memcpy(the_window.surf->colors, old->colors, sizeof old->colors); free(old->s.pixels); free(old); }
    the_window.w = w; the_window.h = h;
}
SDL_Window *SDL_CreateWindow(const char *t, int x, int y, int w, int h, Uint32 flags)
{ (void)t; (void)x; (void)y; the_window.flags = (int)flags; window_resize(w, h); return &the_window; }
void SDL_DestroyWindow(SDL_Window *w) { (void)w; }
SDL_Surface *SDL_GetWindowSurface(SDL_Window *w) { return w->surf ? &w->surf->s : NULL; }
Uint32 SDL_GetWindowFlags(SDL_Window *w) { return (Uint32)w->flags; }
void SDL_SetWindowSize(SDL_Window *w, int ww, int hh) { (void)w; window_resize(ww, hh); }
void SDL_GetWindowSize(SDL_Window *w, int *ww, int *hh) { if (ww) *ww = w->w; if (hh) *hh = w->h; }
int SDL_SetWindowFullscreen(SDL_Window *w, Uint32 flags) { w->flags = (w->flags & ~(int)SDL_WINDOW_FULLSCREEN_DESKTOP) | (int)flags; return 0; }
int SDL_SetWindowDisplayMode(SDL_Window *w, const SDL_DisplayMode *m) { (void)w; (void)m; return 0; }
void SDL_SetWindowBordered(SDL_Window *w, SDL_bool b) { (void)w; (void)b; }
void SDL_SetWindowPosition(SDL_Window *w, int x, int y) { (void)w; (void)x; (void)y; }
int SDL_UpdateWindowSurface(SDL_Window *w)
{
    ShimSurf *s = w->surf;
    hc_call(HC_PRESENT, (long)s->s.pixels, s->s.w, s->s.h, s->s.pitch, (long)s->colors);
    return 0;
}

/* ---------------------------------------------------------------- input */
static HcInput hc_in, hc_prev;
static SDL_Event queue[512]; static int q_head, q_tail, polling;
static int mouse_x, mouse_y;
static void enqueue(const SDL_Event *e) { if ((q_tail + 1) % 512 != q_head) { queue[q_tail] = *e; q_tail = (q_tail + 1) % 512; } }
static SDL_Keymod mods(const unsigned char *k)
{
    int m = 0;
    if (k[SDL_SCANCODE_LSHIFT]) m |= KMOD_LSHIFT; if (k[SDL_SCANCODE_RSHIFT]) m |= KMOD_RSHIFT;
    if (k[SDL_SCANCODE_LCTRL]) m |= KMOD_LCTRL;   if (k[SDL_SCANCODE_RCTRL]) m |= KMOD_RCTRL;
    if (k[SDL_SCANCODE_LALT]) m |= KMOD_LALT;     if (k[SDL_SCANCODE_RALT]) m |= KMOD_RALT;
    return (SDL_Keymod)m;
}
static void step_input(void)
{
    SDL_Event e;
    hc_call(HC_STEP, (long)&hc_in, 0, 0, 0, 0);
    for (int sc = 0; sc < HC_KEYS; sc++) {
        if (hc_in.keys[sc] == hc_prev.keys[sc]) continue;
        memset(&e, 0, sizeof e);
        e.type = hc_in.keys[sc] ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.state = hc_in.keys[sc] ? SDL_PRESSED : SDL_RELEASED;
        e.key.keysym.scancode = (SDL_Scancode)sc;
        e.key.keysym.sym = SDL_GetKeyFromScancode((SDL_Scancode)sc);
        e.key.keysym.mod = (Uint16)mods(hc_in.keys);
        enqueue(&e);
    }
    if (hc_in.mouse_x != hc_prev.mouse_x || hc_in.mouse_y != hc_prev.mouse_y) {
        memset(&e, 0, sizeof e);
        e.type = SDL_MOUSEMOTION; e.motion.x = hc_in.mouse_x; e.motion.y = hc_in.mouse_y;
        e.motion.xrel = hc_in.mouse_x - mouse_x; e.motion.yrel = hc_in.mouse_y - mouse_y;
        mouse_x = hc_in.mouse_x; mouse_y = hc_in.mouse_y;
        enqueue(&e);
    }
    static const Uint8 btn[3] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};
    for (int b = 0; b < 3; b++) {
        int now = (hc_in.buttons >> b) & 1, was = (hc_prev.buttons >> b) & 1;
        if (now == was) continue;
        memset(&e, 0, sizeof e);
        e.type = now ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; e.button.button = btn[b];
        e.button.state = now ? SDL_PRESSED : SDL_RELEASED; e.button.clicks = 1; e.button.x = mouse_x; e.button.y = mouse_y;
        enqueue(&e);
    }
    if (hc_in.quit && !hc_prev.quit) { memset(&e, 0, sizeof e); e.type = SDL_QUIT; enqueue(&e); }
    hc_prev = hc_in;
}
/* An event loop is a run of SDL_PollEvent calls ending with 0. The loop in game_update (the port's frame,
 * core_frame_poll set by patches/0002) ends the step. Other loops re-read the step's input, except that a
 * second one before the next frame is a wait loop spinning on input: it ends a step too. */
int core_frame_poll;
static int extra_polls;
int SDL_PollEvent(SDL_Event *ev)
{
    if (!polling) {
        polling = 1;
        if (core_frame_poll) { extra_polls = 0; step_input(); }
        else if (extra_polls++ >= 1) step_input();
    }
    if (q_head == q_tail) { polling = 0; return 0; }
    if (ev) *ev = queue[q_head];
    q_head = (q_head + 1) % 512;
    return 1;
}
int SDL_PushEvent(SDL_Event *ev) { enqueue(ev); return 1; }
void SDL_PumpEvents(void) { }
void SDL_WarpMouseInWindow(SDL_Window *w, int x, int y) { (void)w; mouse_x = x; mouse_y = y; }
Uint32 SDL_GetMouseState(int *x, int *y) { if (x) *x = mouse_x; if (y) *y = mouse_y; return (Uint32)hc_prev.buttons; }
const Uint8 *SDL_GetKeyboardState(int *n) { if (n) *n = HC_KEYS; return hc_prev.keys; }
SDL_Keymod SDL_GetModState(void) { return mods(hc_prev.keys); }

/* scancode -> keycode (US layout), the part of SDL's default keymap the game can see */
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return SDLK_a + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return SDLK_1 + (sc - SDL_SCANCODE_1);
    switch (sc) {
    case SDL_SCANCODE_0: return SDLK_0;           case SDL_SCANCODE_RETURN: return SDLK_RETURN;
    case SDL_SCANCODE_ESCAPE: return SDLK_ESCAPE; case SDL_SCANCODE_BACKSPACE: return SDLK_BACKSPACE;
    case SDL_SCANCODE_TAB: return SDLK_TAB;       case SDL_SCANCODE_SPACE: return SDLK_SPACE;
    case SDL_SCANCODE_MINUS: return SDLK_MINUS;   case SDL_SCANCODE_EQUALS: return SDLK_EQUALS;
    case SDL_SCANCODE_LEFTBRACKET: return SDLK_LEFTBRACKET; case SDL_SCANCODE_RIGHTBRACKET: return SDLK_RIGHTBRACKET;
    case SDL_SCANCODE_BACKSLASH: return SDLK_BACKSLASH; case SDL_SCANCODE_SEMICOLON: return SDLK_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE: return SDLK_QUOTE; case SDL_SCANCODE_GRAVE: return SDLK_BACKQUOTE;
    case SDL_SCANCODE_COMMA: return SDLK_COMMA;   case SDL_SCANCODE_PERIOD: return SDLK_PERIOD;
    case SDL_SCANCODE_SLASH: return SDLK_SLASH;   case SDL_SCANCODE_CAPSLOCK: return SDLK_CAPSLOCK;
    case SDL_SCANCODE_F1: return SDLK_F1;  case SDL_SCANCODE_F2: return SDLK_F2;  case SDL_SCANCODE_F3: return SDLK_F3;
    case SDL_SCANCODE_F4: return SDLK_F4;  case SDL_SCANCODE_F5: return SDLK_F5;  case SDL_SCANCODE_F6: return SDLK_F6;
    case SDL_SCANCODE_F7: return SDLK_F7;  case SDL_SCANCODE_F8: return SDLK_F8;  case SDL_SCANCODE_F9: return SDLK_F9;
    case SDL_SCANCODE_F10: return SDLK_F10; case SDL_SCANCODE_F11: return SDLK_F11; case SDL_SCANCODE_F12: return SDLK_F12;
    case SDL_SCANCODE_PRINTSCREEN: return SDLK_PRINTSCREEN; case SDL_SCANCODE_SCROLLLOCK: return SDLK_SCROLLLOCK;
    case SDL_SCANCODE_PAUSE: return SDLK_PAUSE;   case SDL_SCANCODE_INSERT: return SDLK_INSERT;
    case SDL_SCANCODE_HOME: return SDLK_HOME;     case SDL_SCANCODE_PAGEUP: return SDLK_PAGEUP;
    case SDL_SCANCODE_DELETE: return SDLK_DELETE; case SDL_SCANCODE_END: return SDLK_END;
    case SDL_SCANCODE_PAGEDOWN: return SDLK_PAGEDOWN; case SDL_SCANCODE_RIGHT: return SDLK_RIGHT;
    case SDL_SCANCODE_LEFT: return SDLK_LEFT;     case SDL_SCANCODE_DOWN: return SDLK_DOWN;
    case SDL_SCANCODE_UP: return SDLK_UP;         case SDL_SCANCODE_NUMLOCKCLEAR: return SDLK_NUMLOCKCLEAR;
    case SDL_SCANCODE_KP_DIVIDE: return SDLK_KP_DIVIDE; case SDL_SCANCODE_KP_MULTIPLY: return SDLK_KP_MULTIPLY;
    case SDL_SCANCODE_KP_MINUS: return SDLK_KP_MINUS; case SDL_SCANCODE_KP_PLUS: return SDLK_KP_PLUS;
    case SDL_SCANCODE_KP_ENTER: return SDLK_KP_ENTER; case SDL_SCANCODE_KP_PERIOD: return SDLK_KP_PERIOD;
    case SDL_SCANCODE_KP_1: return SDLK_KP_1; case SDL_SCANCODE_KP_2: return SDLK_KP_2; case SDL_SCANCODE_KP_3: return SDLK_KP_3;
    case SDL_SCANCODE_KP_4: return SDLK_KP_4; case SDL_SCANCODE_KP_5: return SDLK_KP_5; case SDL_SCANCODE_KP_6: return SDLK_KP_6;
    case SDL_SCANCODE_KP_7: return SDLK_KP_7; case SDL_SCANCODE_KP_8: return SDLK_KP_8; case SDL_SCANCODE_KP_9: return SDLK_KP_9;
    case SDL_SCANCODE_KP_0: return SDLK_KP_0;
    case SDL_SCANCODE_LCTRL: return SDLK_LCTRL;   case SDL_SCANCODE_LSHIFT: return SDLK_LSHIFT;
    case SDL_SCANCODE_LALT: return SDLK_LALT;     case SDL_SCANCODE_RCTRL: return SDLK_RCTRL;
    case SDL_SCANCODE_RSHIFT: return SDLK_RSHIFT; case SDL_SCANCODE_RALT: return SDLK_RALT;
    default: return SDLK_UNKNOWN;
    }
}
