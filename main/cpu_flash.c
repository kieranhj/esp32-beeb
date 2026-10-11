// Phase 0 variant: core left in flash (runs from icache), empty hook.
#include "esp_attr.h"
#define CPU6502_FN(name) name##_flash
#include "phase0.h"
#define CPU6502_CODE_ATTR
#define CPU6502_TABLE_ATTR DRAM_ATTR
#include "cpu6502.c"
