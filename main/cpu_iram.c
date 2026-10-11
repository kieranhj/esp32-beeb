// Phase 0 variant: core in IRAM, empty per-cycle hook. The baseline.
#include "esp_attr.h"
#define CPU6502_FN(name) name##_iram
#include "phase0.h"
#define CPU6502_CODE_ATTR IRAM_ATTR
#define CPU6502_TABLE_ATTR DRAM_ATTR
#include "cpu6502.c"
