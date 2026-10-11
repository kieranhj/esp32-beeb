// ESP32 build of the machine: one translation unit with the core, VIAs and
// CRTC inlined, hot code in IRAM and the dispatch tables in DRAM.
#include "esp_attr.h"
#define MACHINE_CODE_ATTR IRAM_ATTR
#define CPU6502_TABLE_ATTR DRAM_ATTR
#include "machine.c"
