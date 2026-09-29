/* wbx-entry.c - the miniBox guest ABI: the exports the Chimera frontend calls. Guest build only;
 * run-native.c drives the same driver natively. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emulibc.h>
#include <waterbox_settings.h>
#include "syndicatfx-driver.h"
#include "game-state.h"

static char load_error[512];
static int inited;

/* mounted files: read-only, opened by exact name */
void *drv_file_open(const char *name, size_t *size)
{
    FILE *f = fopen(name, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(n > 0 ? (size_t)n : 1);
    if (!b || (n > 0 && fread(b, 1, (size_t)n, f) != (size_t)n)) { fclose(f); free(b); return NULL; }
    fclose(f); *size = (size_t)n; return b;
}
void *drv_alloc_readonly(size_t size) { return alloc_sealed(size); }
void drv_readonly_done(void) { }

ECL_EXPORT const char *GetLoadError(void) { return load_error; }

ECL_EXPORT int Init(void)
{
    char lang[32] = "English", sound[32] = "Sound Blaster";
    load_error[0] = 0;
    wbx_setting_str("language", lang, sizeof lang);
    wbx_setting_str("sound", sound, sizeof sound);
    if (drv_init(lang, strcmp(sound, "None") != 0, load_error, sizeof load_error) < 0) return 0;
    inited = 1;
    return 1;
}

ECL_EXPORT int IsButtonActive(int32_t index) { return index >= 0 && index < SFX_BUTTON_COUNT; }
ECL_EXPORT void SetButton(int32_t index, int32_t state) { drv_set_button(index, state); }
ECL_EXPORT void SetAxis(int32_t index, int32_t value) { drv_set_axis(index, value); }

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
    if (!inited) return;
    drv_set_packed(packed);
    drv_step();
}

ECL_EXPORT void SetRenderingEnabled(int on) { drv_set_rendering(on); }
ECL_EXPORT uint32_t *GetVideoBgra(void) { return (uint32_t *)drv_video(NULL, NULL); }
ECL_EXPORT int GetVideoWidth(void) { int w, h; drv_video(&w, &h); return w; }
ECL_EXPORT int GetVideoHeight(void) { int w, h; drv_video(&w, &h); return h; }
ECL_EXPORT int GetDisplayAspectX(void) { return 4; }
ECL_EXPORT int GetDisplayAspectY(void) { return 3; }
ECL_EXPORT int16_t *GetAudio(void) { int n; return (int16_t *)drv_audio(&n); }
ECL_EXPORT int GetAudioSampleCount(void) { int n; drv_audio(&n); return n; }
ECL_EXPORT int GetVsyncNumerator(void) { int n, d; drv_vsync(&n, &d); return n; }
ECL_EXPORT int GetVsyncDenominator(void) { int n, d; drv_vsync(&n, &d); return d; }
ECL_EXPORT int InputWasRead(void) { return drv_input_was_read(); }
ECL_EXPORT uint64_t GetCycleCount(void) { return drv_cycles(); }

ECL_EXPORT int GetMemoryDomainCount(void) { return gs_domain_count(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return gs_domain_name(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return gs_domain_ptr(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return gs_domain_size(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return gs_domain_writable(i); }
ECL_EXPORT const char *GetGameProperties(void) { return gs_properties_json(); }
