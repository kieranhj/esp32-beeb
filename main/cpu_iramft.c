// Phase 0 variant: core in IRAM, dispatch table left in flash rodata (read
// through the data cache) to measure what DRAM placement of tables buys.
#include "esp_attr.h"
#define CPU6502_FN(name) name##_iramft
#include "phase0.h"
#define CPU6502_CODE_ATTR IRAM_ATTR
#define CPU6502_TABLE_ATTR
#include "cpu6502.c"
