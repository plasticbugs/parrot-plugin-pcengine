// nocd.c -- no CD-ROM drive: what pce-go's pce.c and pce-go.c call in cd.c
// and cd_chd.c (left out, see vendor/pce-go/VENDORED.md), answered as a
// console with nothing in its expansion port. CD.cd_attached stays false,
// so the core never reads or writes the drive's ports.
#include "cd.h"
#include "cd_chd.h"
#include "ff.h"

cd_state_t CD;

int cd_init(void) { return 0; }
void cd_reset(void) {}
void cd_term(void) {}
uint8_t cd_read(uint16_t addr) { (void)addr; return 0xFF; }
void cd_write(uint16_t addr, uint8_t val) { (void)addr; (void)val; }
uint8_t cd_acd_read_bank(uint8_t port) { (void)port; return 0xFF; }
void cd_acd_write_bank(uint8_t port, uint8_t val) { (void)port; (void)val; }
void cd_subcode_tick(void) {}
// The pad port's bit 6: clear, as on a TurboGrafx-16. US games check it
// and stop on a PC Engine; Japanese ones don't look.
bool cd_bios_is_us(void) { return false; }

// LoadDisc()'s (never called)
void cd_setup_memory_map(void) {}
int cd_load_cue(const char *cue_path) { (void)cue_path; return -1; }
void cd_close(void) {}
int cd_chd_open(const char *path) { (void)path; return -1; }
int cd_find_bios(char *out_path, size_t size, const char *primary_dir, bios_variant_t *out_variant) {
    (void)out_path; (void)size; (void)primary_dir; (void)out_variant;
    return -1;
}
FRESULT f_open(FIL *fp, const char *path, uint8_t mode) { (void)fp; (void)path; (void)mode; return FR_DISK_ERR; }
FRESULT f_close(FIL *fp) { (void)fp; return FR_OK; }
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) { (void)fp; (void)buff; (void)btr; *br = 0; return FR_DISK_ERR; }
