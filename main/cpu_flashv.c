// Phase 0 variant: core with the video hook left in flash. At 43 KB it is
// bigger than the 32 KB icache, so this measures a real icache miss storm.
#include "esp_attr.h"
#define CPU6502_FN(name) name##_flashv
#include "phase0.h"
#define CPU6502_CODE_ATTR
#define CPU6502_TABLE_ATTR DRAM_ATTR
#define CPU6502_TICK(cpu) phase0_video_tick(cpu)
#include "cpu6502.c"
