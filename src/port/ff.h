// ff.h -- a stand-in for FatFS's, for pce-go.c's LoadDisc() (the CD-ROM,
// which this plugin leaves out: nothing calls it). Only what it names.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define FF_MAX_LFN 255
#define FA_READ 0x01

typedef unsigned int UINT;
typedef uint64_t FSIZE_t;
typedef enum { FR_OK = 0, FR_DISK_ERR } FRESULT;
typedef struct { FSIZE_t size; } FIL;

FRESULT f_open(FIL *fp, const char *path, uint8_t mode);
FRESULT f_close(FIL *fp);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
static inline FSIZE_t f_size(FIL *fp) { return fp->size; }
