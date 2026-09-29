/* Stubs for what the core build leaves out: bfsoundlib (the core starts the game without sound, -s)
 * and bflibrary's gpng.c (PNG screenshots). Filled from the link's undefined references. */
#include "bftypes.h"
#include "bfpng.h"
TbResult LbPngLoad(const char *fname, TbPixel *out_buffer, ulong *width, ulong *height, ubyte *pal) { return Lb_FAIL; }
TbResult LbPngSave(const char *fname, const TbPixel *inp_buffer, ulong width, ulong height, const ubyte *pal, TbBool force_fname) { return Lb_FAIL; }
TbResult LbPngSaveScreen(const char *fname, const TbPixel *inp_buffer, const ubyte *pal, TbBool force_fname) { return Lb_FAIL; }
