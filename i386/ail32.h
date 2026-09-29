/* ail32.h - the AIL/32 2.x API the original game drives its music driver with (GAMEFM.DLL), as the
 * core provides it: ail32.c, a C port of the AIL/32 routines linked into MAIN.EXE. */
#ifndef AIL32_H
#define AIL32_H
#include <stdint.h>

typedef int32_t HDRIVER;
typedef int32_t HSEQUENCE;

/* the driver descriptor (AIL_describe_driver) */
typedef struct {
    uint32_t min_API_version;
    uint32_t drvr_type;            /* 2 digital, 3 XMIDI */
    char data_suffix[4];           /* the Global Timbre Library's extension: "AD" */
    void *dev_name_table;
    uint32_t default_IO, default_IRQ, default_DMA, default_DRQ;
    uint32_t service_rate;         /* Hz, or -1 */
    uint32_t display_size;
} AIL32_DESC;

void ail32_startup(void);
void ail32_shutdown(const char *msg);
HDRIVER ail32_register_driver(void *driver_base);
AIL32_DESC *ail32_describe_driver(HDRIVER h);
int32_t ail32_detect_device(HDRIVER h, uint32_t io, uint32_t irq, uint32_t dma, uint32_t drq);
void ail32_init_driver(HDRIVER h, uint32_t io, uint32_t irq, uint32_t dma, uint32_t drq);
uint32_t ail32_state_table_size(HDRIVER h);
HSEQUENCE ail32_register_sequence(HDRIVER h, void *FORM_XMID, uint32_t sequence_num, void *state_table, void *controller_table);
uint32_t ail32_default_timbre_cache_size(HDRIVER h);
void ail32_define_timbre_cache(HDRIVER h, void *cache_addr, uint32_t cache_size);
uint32_t ail32_timbre_request(HDRIVER h, HSEQUENCE s);
void ail32_install_timbre(HDRIVER h, uint32_t bank, uint32_t patch, void *src_addr);
void ail32_start_sequence(HDRIVER h, HSEQUENCE s);
void ail32_stop_sequence(HDRIVER h, HSEQUENCE s);
void ail32_resume_sequence(HDRIVER h, HSEQUENCE s);
uint32_t ail32_sequence_status(HDRIVER h, HSEQUENCE s);

/* DLL_load: a DOS/4GW LX module in memory, loaded and relocated into memory of its own */
void *ail32_dll_load(const void *image, uint32_t image_size);

/* the timer interrupt: runs the ticks that are due (the program's step end, and every API call) */
void ail32_service(void);
#endif
