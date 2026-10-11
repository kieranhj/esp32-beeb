// Phase 0 variant: core in IRAM, switch dispatch (GCC compare tree) instead
// of the computed-goto table, to measure what the table buys.
#include "esp_attr.h"
#define CPU6502_FN(name) name##_iramsw
#include "phase0.h"
#define CPU6502_CODE_ATTR IRAM_ATTR
#define CPU6502_USE_SWITCH 1
#include "cpu6502.c"
