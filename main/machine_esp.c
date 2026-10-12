// ESP32 build of the machine: one translation unit with the core, VIAs and
// CRTC inlined, hot code in IRAM and the dispatch tables in DRAM.
#include "esp_attr.h"
#include "esp_cpu.h"
#define MACHINE_CODE_ATTR IRAM_ATTR
// BEEB_PROFILE (CMake) turns the machine's path counters on: 13 KB of
// IRAM, so off in normal builds.
#ifdef BEEB_PROFILE
#define MACHINE_STATS 1
#define MACHINE_CYCLES() esp_cpu_get_cycle_count()
#endif
#define CPU6502_TABLE_ATTR DRAM_ATTR
#include "machine.c"
