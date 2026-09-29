/* gate-harness.h - the shared half of run-native.c and run-wbx.c (from chimera-core-opensamurai,
 * with the mouse: axes and buttons).
 *
 * One replay-and-digest loop over an abstract core interface, so the native
 * reference and the sandboxed core run EXACTLY the same schedule and are
 * digested the same way: every step's picture, sound, length and lag, the
 * core's clock, and every memory domain.
 *
 * Input is a movie: one line per step, the buttons held on that step, as tokens separated by spaces:
 * a token of single characters holds each one (gate_key_index: a letter or a digit is its key, U D L R
 * the arrows, N Enter, _ Space, B Backspace, X Esc, S Shift, [ the left mouse button, ] the right one);
 * F1..F12 are tokens of their own; x=N and y=N set the mouse axes (0..65535 over the picture), which
 * keep their value until set again. A line starting with # is a comment. --movie-at puts its first line
 * at a given step; --press adds keys held for a stretch of steps.
 *
 * Properties are reached the way the frontend reaches them: through the table
 * GetGameProperties exports (name -> domain, offset, type), never through the
 * core's own headers - so a poke here tests the export, not the harness.
 */
#ifndef GATE_HARNESS_H
#define GATE_HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sfx-tables.h"

#define GATE_BTN_COUNT SFX_BUTTON_COUNT

/* The core answers no fopen of its own (the game's files are its machine's): the harness's files are real. */
#define gate_fopen fopen

struct gate_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	void (*set_button)(int32_t index, int32_t state);
	void (*set_axis)(int32_t index, int32_t value);
	void (*frame)(void);
	const uint32_t *(*video)(int *w, int *h);
	const int16_t *(*audio)(int *n);
	int (*input_was_read)(void);
	int (*domain_count)(void);
	const char *(*domain_name)(int i);
	uint8_t *(*domain_ptr)(int i);
	int64_t (*domain_size)(int i);
	int (*vsync_numerator)(void);
	int (*vsync_denominator)(void);
	void (*pre_frame)(long frame); /* optional (the rerecord and session legs) */
	void (*set_rendering)(int on);  /* optional (the turbo leg) */
	uint64_t (*clock)(void);
	const char *(*game_properties)(void);
	int (*button_active)(int32_t index);   /* optional: IsButtonActive */
};

#define GATE_MAX_PRESS 64
#define GATE_MAX_SHOTS 16
#define GATE_MAX_POKES 32

struct gate_press { long at, len; char keys[48]; };
struct gate_shot { long at; const char *path; };
struct gate_poke { long from, to; char name[64]; double value; };

struct gate_opts
{
	long frames;
	const char *moviePath;
	long movieAt;
	struct gate_press press[GATE_MAX_PRESS];
	int npress;
	struct gate_shot shots[GATE_MAX_SHOTS];
	int nshots;
	struct gate_poke pokes[GATE_MAX_POKES]; /* from == to: a poke; from < to: a freeze */
	int npokes;
	const char *tracePath;   /* per step: frame, rate, input read, then the traced properties */
	const char *traceProps;  /* comma-separated property names */
	const char *propsJson;   /* GetGameProperties, written out */
	const char *dumpDomain;
	const char *dumpPath;
	const char *audioOut;
	int turbo;
	long turboSettle;
};

static uint64_t gate_fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	if (!h) h = 1469598103934665603ULL;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
	return h;
}

/* ------------------------------------------------------------ the movie */

/* A key of the movie: a lowercase letter or a digit is that key; the rest
 * are named by a character of their own. */
static int gate_button_named(const char *name)
{
	for (int i = 0; i < GATE_BTN_COUNT; i++) if (!strcmp(sfx_buttons[i].name, name)) return i;
	return -1;
}
static int gate_key_index(char c)
{
	char n[32];
	if (c >= 'a' && c <= 'z') { snprintf(n, sizeof n, "Key %c", c - 32); return gate_button_named(n); }
	if (c >= '0' && c <= '9') { snprintf(n, sizeof n, "Key %c", c); return gate_button_named(n); }
	switch (c)
	{
	case 'U': return gate_button_named("Key Up");
	case 'D': return gate_button_named("Key Down");
	case 'L': return gate_button_named("Key Left");
	case 'R': return gate_button_named("Key Right");
	case 'N': return gate_button_named("Key Enter");
	case '_': return gate_button_named("Key Space");
	case 'B': return gate_button_named("Key Backspace");
	case 'X': return gate_button_named("Key Escape");
	case 'S': return gate_button_named("Key LeftShift");
	case '[': return gate_button_named("Mouse Left Button");
	case ']': return gate_button_named("Mouse Right Button");
	default: return -1;
	}
}

static char **gate_movie;
static long gate_movie_len;

static int gate_load_movie(const char *path)
{
	FILE *f = gate_fopen(path, "r");
	if (!f) { perror(path); return 0; }
	char line[256];
	long cap = 0;
	while (fgets(line, sizeof line, f))
	{
		if (line[0] == '#') continue; /* a comment, not a step */
		if (gate_movie_len == cap)
		{
			cap = cap ? cap * 2 : 1024;
			gate_movie = (char **)realloc(gate_movie, (size_t)cap * sizeof *gate_movie);
		}
		gate_movie[gate_movie_len++] = strdup(line);
	}
	fclose(f);
	return 1;
}

static void gate_apply_keys(const char *keys, uint8_t *buttons, int32_t *axes)
{
	char tok[64];
	for (const char *k = keys; *k; )
	{
		while (*k == ' ' || *k == '\t' || *k == '\n' || *k == '\r') k++;
		int n = 0;
		while (k[n] && k[n] != ' ' && k[n] != '\t' && k[n] != '\n' && k[n] != '\r' && n < 63) n++;
		if (!n) break;
		memcpy(tok, k, (size_t)n); tok[n] = 0; k += n;
		if ((tok[0] == 'x' || tok[0] == 'y') && tok[1] == '=') { if (axes) axes[tok[0] == 'y'] = (int32_t)strtol(tok + 2, NULL, 10); continue; }
		if (tok[0] == 'F' && tok[1] >= '1' && tok[1] <= '9') { char nm[16]; snprintf(nm, sizeof nm, "Key %s", tok); int i = gate_button_named(nm); if (i >= 0) buttons[i] = 1; continue; }
		for (const char *c = tok; *c; c++) { int i = gate_key_index(*c); if (i >= 0) buttons[i] = 1; }
	}
}

/* ------------------------------------------------------------ properties */

struct gate_prop { int domain; long offset; char type[8]; int bit, bits; };

/* The property's place, from the exported table. The table is JSON; its
 * entries are one object each, with "name", "domain", "offset" and "type" in
 * that order and the optional fields after them (check-properties.py holds
 * the core to the full format). "Name[i]" is element i of an array. */
static int gate_find_prop(const struct gate_core *c, const char *name, struct gate_prop *out)
{
	const char *json = c->game_properties ? c->game_properties() : NULL;
	if (!json) return 0;
	char base[96];
	long index = 0;
	snprintf(base, sizeof base, "%s", name);
	char *bracket = strrchr(base, '[');
	if (bracket && base[strlen(base) - 1] == ']')
	{
		index = strtol(bracket + 1, NULL, 10);
		*bracket = 0;
	}
	char key[128];
	snprintf(key, sizeof key, "\"name\": \"%s\",", base);
	const char *p = strstr(json, key);
	if (!p) return 0;
	const char *end = strchr(p, '}');
	char domain[64] = "";
	const char *d = strstr(p, "\"domain\": \"");
	const char *o = strstr(p, "\"offset\": ");
	const char *t = strstr(p, "\"type\": \"");
	if (!d || !o || !t || !end || t > end) return 0;
	sscanf(d + 11, "%63[^\"]", domain);
	out->offset = strtol(o + 10, NULL, 10);
	sscanf(t + 9, "%7[^\"]", out->type);
	const char *st = strstr(p, "\"stride\": ");
	if (index && st && st < end) out->offset += index * strtol(st + 10, NULL, 10);
	const char *bi = strstr(p, "\"bit\": "), *bs = strstr(p, "\"bits\": ");
	out->bit = bi && bi < end ? (int)strtol(bi + 7, NULL, 10) : 0;
	out->bits = bs && bs < end ? (int)strtol(bs + 8, NULL, 10) : 0;
	out->domain = -1;
	for (int i = 0; i < c->domain_count(); i++)
		if (!strcmp(c->domain_name(i), domain)) out->domain = i;
	return out->domain >= 0;
}

static int gate_size(const char *type)
{
	return type[1] == '8' || !strcmp(type, "bool") ? 1 : type[1] == '1' ? 2 : type[1] == '6' ? 8 : 4;
}

static double gate_read_prop(const struct gate_core *c, const struct gate_prop *p)
{
	const uint8_t *b = c->domain_ptr(p->domain) + p->offset;
	if (!strcmp(p->type, "f32")) { float v; memcpy(&v, b, 4); return v; }
	uint64_t raw = 0;
	memcpy(&raw, b, (size_t)gate_size(p->type));
	if (p->bits) raw = (raw >> p->bit) & ((1ull << p->bits) - 1);
	if (p->type[0] == 's' && !p->bits)
	{
		const int n = gate_size(p->type) * 8;
		if (n < 64 && (raw >> (n - 1)) & 1) raw |= ~0ull << n;
		return (double)(int64_t)raw;
	}
	return (double)raw;
}

static void gate_write_prop(const struct gate_core *c, const struct gate_prop *p, double value)
{
	uint8_t *b = c->domain_ptr(p->domain) + p->offset;
	if (!strcmp(p->type, "f32")) { float v = (float)value; memcpy(b, &v, 4); return; }
	const size_t n = (size_t)gate_size(p->type);
	uint64_t raw = 0, v = (uint64_t)(int64_t)value;
	memcpy(&raw, b, n);
	if (p->bits)
	{
		const uint64_t mask = ((1ull << p->bits) - 1) << p->bit;
		raw = (raw & ~mask) | ((v << p->bit) & mask);
	}
	else
		raw = v;
	memcpy(b, &raw, n);
}

/* ------------------------------------------------------------ pictures */

static int gate_write_tga(const char *path, const uint32_t *bgra, int w, int h)
{
	FILE *f = gate_fopen(path, "wb");
	if (!f) return 0;
	uint8_t hdr[18] = { 0 };
	hdr[2] = 2;
	hdr[12] = w & 0xff; hdr[13] = (w >> 8) & 0xff;
	hdr[14] = h & 0xff; hdr[15] = (h >> 8) & 0xff;
	hdr[16] = 32;
	hdr[17] = 0x20;
	fwrite(hdr, 1, 18, f);
	fwrite(bgra, 4, (size_t)w * h, f);
	fclose(f);
	return 1;
}

/* ------------------------------------------------------------ the run */

static int gate_run(const struct gate_core *c, const struct gate_opts *o)
{
	if (c->init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c->load_error ? c->load_error() : "?");
		printf("loadError=%s\n", c->load_error ? c->load_error() : "?");
		return 1;
	}
	if (c->button_active)
	{
		/* the buttons the core says do something, as the frontend asks */
		int n = 0;
		for (int i = 0; i < GATE_BTN_COUNT; i++) n += c->button_active(i) ? 1 : 0;
		printf("activeButtons=%d\n", n);
	}
	if (o->moviePath && !gate_load_movie(o->moviePath))
		return 1;
	if (o->propsJson)
	{
		FILE *f = gate_fopen(o->propsJson, "w");
		if (!f) { perror(o->propsJson); return 1; }
		fputs(c->game_properties ? c->game_properties() : "", f);
		fclose(f);
	}

	/* the traced properties and the pokes, found once in the table */
	struct gate_prop traced[32];
	int ntraced = 0;
	if (o->traceProps)
	{
		char list[1024];
		snprintf(list, sizeof list, "%s", o->traceProps);
		for (char *tok = strtok(list, ","); tok && ntraced < 32; tok = strtok(NULL, ","))
			if (!gate_find_prop(c, tok, &traced[ntraced++]))
			{
				fprintf(stderr, "no such property: %s\n", tok);
				return 1;
			}
	}
	struct gate_prop poked[GATE_MAX_POKES];
	for (int i = 0; i < o->npokes; i++)
		if (!gate_find_prop(c, o->pokes[i].name, &poked[i]))
		{
			fprintf(stderr, "no such property: %s\n", o->pokes[i].name);
			return 1;
		}
	FILE *trace = o->tracePath ? gate_fopen(o->tracePath, "w") : NULL;
	if (trace)
	{
		/* the machine as Init left it */
		fprintf(trace, "init - -");
		for (int i = 0; i < ntraced; i++)
			fprintf(trace, " %.9g", gate_read_prop(c, &traced[i]));
		fputc('\n', trace);
	}

	const long frames = o->frames;
	const long tail = frames / 2;
	const long hashFrom = tail + o->turboSettle;
	uint64_t vh = 0, th = 0, ah = 0, sh = 0;
	long lag = 0;
	uint8_t buttons[GATE_BTN_COUNT], prev[GATE_BTN_COUNT];
	int32_t axes[SFX_AXIS_COUNT] = {32768, 32768};
	memset(prev, 0, sizeof prev);

	for (long f = 0; f < frames; f++)
	{
		memset(buttons, 0, sizeof buttons);
		if (o->moviePath && f >= o->movieAt && f - o->movieAt < gate_movie_len)
			gate_apply_keys(gate_movie[f - o->movieAt], buttons, axes);
		for (int i = 0; i < o->npress; i++)
			if (f >= o->press[i].at && f < o->press[i].at + o->press[i].len)
				gate_apply_keys(o->press[i].keys, buttons, axes);

		if (o->turbo && c->set_rendering)
			c->set_rendering(f >= tail);
		if (c->pre_frame)
			c->pre_frame(f);

		/* pokes land in the domain before the step, as a frontend's would */
		for (int i = 0; i < o->npokes; i++)
			if (f >= o->pokes[i].from && f <= o->pokes[i].to)
				gate_write_prop(c, &poked[i], o->pokes[i].value);

		for (int i = 0; i < GATE_BTN_COUNT; i++)
		{
			if (buttons[i] != prev[i])
				c->set_button(i, buttons[i]);
			prev[i] = buttons[i];
		}
		/* the frontend sets the axes before every step */
		for (int i = 0; i < SFX_AXIS_COUNT; i++) c->set_axis(i, axes[i]);

		c->frame();

		int w = 0, h = 0, n = 0;
		const uint32_t *video = c->video(&w, &h);
		const int16_t *audio = c->audio(&n);
		const int num = c->vsync_numerator(), den = c->vsync_denominator();
		const int read = c->input_was_read();
		vh = gate_fnv(vh, &w, sizeof w);
		vh = gate_fnv(vh, &h, sizeof h);
		vh = gate_fnv(vh, video, (size_t)w * h * 4);
		if (f >= hashFrom)
		{
			th = gate_fnv(th, &w, sizeof w);
			th = gate_fnv(th, &h, sizeof h);
			th = gate_fnv(th, video, (size_t)w * h * 4);
		}
		ah = gate_fnv(ah, audio, (size_t)n * 2 * sizeof(int16_t));
		sh = gate_fnv(sh, &num, sizeof num);
		sh = gate_fnv(sh, &den, sizeof den);
		sh = gate_fnv(sh, &n, sizeof n);
		sh = gate_fnv(sh, &read, sizeof read);
		if (o->audioOut)
		{
			FILE *af = gate_fopen(o->audioOut, f == 0 ? "wb" : "ab");
			if (af) { fwrite(audio, 2 * sizeof(int16_t), (size_t)n, af); fclose(af); }
		}
		if (!read)
			lag++;
		if (trace)
		{
			fprintf(trace, "%ld %d/%d %d", f, num, den, read);
			for (int i = 0; i < ntraced; i++)
				fprintf(trace, " %.9g", gate_read_prop(c, &traced[i]));
			fputc('\n', trace);
		}
		for (int i = 0; i < o->nshots; i++)
			if (o->shots[i].at == f)
				gate_write_tga(o->shots[i].path, video, w, h);
	}
	if (trace) fclose(trace);

	printf("frames=%ld\n", frames);
	printf("vsync=%d/%d\n", c->vsync_numerator(), c->vsync_denominator());
	printf("videoHash=%016llx\n", (unsigned long long)vh);
	printf("tailVideoHash=%016llx\n", (unsigned long long)th);
	printf("audioHash=%016llx\n", (unsigned long long)ah);
	printf("stepsHash=%016llx\n", (unsigned long long)sh);
	printf("lagFrames=%ld\n", lag);
	printf("clock=%llu\n", (unsigned long long)c->clock());
	for (int i = 0; i < c->domain_count(); i++)
	{
		uint64_t dh = gate_fnv(0, c->domain_ptr(i), (size_t)c->domain_size(i));
		printf("domain[%s]=%016llx\n", c->domain_name(i), (unsigned long long)dh);
	}
	for (int i = 0; i < c->domain_count(); i++)
		printf("domainSize[%s]=%lld\n", c->domain_name(i), (long long)c->domain_size(i));

	if (o->dumpDomain && o->dumpPath)
	{
		int found = 0;
		for (int i = 0; i < c->domain_count(); i++)
		{
			if (strcmp(c->domain_name(i), o->dumpDomain) != 0) continue;
			FILE *f = gate_fopen(o->dumpPath, "wb");
			if (!f) { perror(o->dumpPath); return 1; }
			fwrite(c->domain_ptr(i), 1, (size_t)c->domain_size(i), f);
			fclose(f);
			found = 1;
		}
		if (!found) { fprintf(stderr, "no such domain to dump: %s\n", o->dumpDomain); return 1; }
	}
	return 0;
}

/* shared CLI parsing; 0 on a bad argument */
static int gate_parse_opts(int argc, char **argv, int first, struct gate_opts *o)
{
	memset(o, 0, sizeof *o);
	o->frames = 600;
	for (int i = first; i < argc; i++)
	{
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) o->frames = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--movie") && i + 1 < argc) o->moviePath = argv[++i];
		else if (!strcmp(argv[i], "--movie-at") && i + 1 < argc) o->movieAt = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--press") && i + 1 < argc && o->npress < GATE_MAX_PRESS)
		{
			/* FRAME:KEYS[:LENGTH] */
			struct gate_press *p = &o->press[o->npress++];
			p->len = 1;
			if (sscanf(argv[++i], "%ld:%47[^:]:%ld", &p->at, p->keys, &p->len) < 2) { fprintf(stderr, "bad --press %s\n", argv[i]); return 0; }
		}
		else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc && o->nshots < GATE_MAX_SHOTS)
		{
			/* FRAME:PATH (the picture after that step) */
			char *arg = argv[++i];
			char *colon = strchr(arg, ':');
			if (!colon) { fprintf(stderr, "bad --screenshot %s\n", arg); return 0; }
			*colon = 0;
			o->shots[o->nshots].at = strtol(arg, 0, 0);
			o->shots[o->nshots++].path = colon + 1;
		}
		else if ((!strcmp(argv[i], "--poke") || !strcmp(argv[i], "--freeze")) && i + 1 < argc && o->npokes < GATE_MAX_POKES)
		{
			/* --poke FRAME:NAME=VALUE (before that step), --freeze FROM-TO:NAME=VALUE (before each) */
			struct gate_poke *p = &o->pokes[o->npokes++];
			const int freeze = argv[i][2] == 'f';
			char *arg = argv[++i];
			int ok = freeze ? sscanf(arg, "%ld-%ld:%63[^=]=%lf", &p->from, &p->to, p->name, &p->value) == 4
			                : sscanf(arg, "%ld:%63[^=]=%lf", &p->from, p->name, &p->value) == 3;
			if (!freeze) p->to = p->from;
			if (!ok) { fprintf(stderr, "bad %s %s\n", freeze ? "--freeze" : "--poke", arg); return 0; }
		}
		else if (!strcmp(argv[i], "--trace") && i + 1 < argc) o->tracePath = argv[++i];
		else if (!strcmp(argv[i], "--trace-props") && i + 1 < argc) o->traceProps = argv[++i];
		else if (!strcmp(argv[i], "--props-json") && i + 1 < argc) o->propsJson = argv[++i];
		else if (!strcmp(argv[i], "--dump-domain") && i + 2 < argc) { o->dumpDomain = argv[++i]; o->dumpPath = argv[++i]; }
		else if (!strcmp(argv[i], "--audio") && i + 1 < argc) o->audioOut = argv[++i];
		else if (!strcmp(argv[i], "--turbo")) o->turbo = 1;
		else if (!strcmp(argv[i], "--turbo-settle") && i + 1 < argc) o->turboSettle = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--rerecord") || !strcmp(argv[i], "--session")) ; /* run-wbx's */
		else if (!strcmp(argv[i], "--session-at") && i + 1 < argc) i++;                /* run-wbx's */
		else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 0; }
	}
	return 1;
}

#endif /* GATE_HARNESS_H */
