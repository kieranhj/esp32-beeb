// esp32-beeb: the board's inputs mapped to the machine, for playing demos
// without a keyboard.
//
//   touch                 Space held while the finger is down
//   long press (> 0.8 s)  Return
//   swipe along           next / previous disc (reboots)
//   swipe across          the disc menu (reboots into it, menu.h)
//   PWR button short      restart (BREAK with Shift: the disc autoboots)
//   PWR held 2 s          sound off / on
//   shake                 restart, as PWR
//
// The CST816/CST820 touch controller (I2C 0x15) and the TCA9554 expander
// carrying the PWR button (0x20) are polled by a task on core 0; the keys
// go to the machine's loop on core 1 through a mailbox.
#ifndef INPUT_H
#define INPUT_H

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t input_init(i2c_master_bus_handle_t i2c);

// The next key event for the machine, if any: row, column, down.
int input_take_key(int *row, int *col, int *down);

// Menu mode: touches become gestures for the menu instead of keys.
enum { INPUT_TAP, INPUT_SWIPE_FORWARD, INPUT_SWIPE_BACK, INPUT_SWIPE_ACROSS };
void input_set_menu(int on);
// The next gesture, if any. A tap's position is in picture coordinates
// (x along the long side, 0 to 447; y down the short side, 0 to 367).
int input_take_gesture(int *kind, int *x, int *y);

#endif
