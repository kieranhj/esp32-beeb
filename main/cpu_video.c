// Phase 0 variant: core in IRAM, per-cycle hook does representative video work.
#include "esp_attr.h"
#define CPU6502_FN(name) name##_video
#include "phase0.h"
#define CPU6502_CODE_ATTR IRAM_ATTR
#define CPU6502_TABLE_ATTR DRAM_ATTR
#define CPU6502_TICK(cpu) phase0_video_tick(cpu)
#include "cpu6502.c"
