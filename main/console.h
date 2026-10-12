// esp32-beeb: a line console on the USB serial port, for driving the board
// from the PC before there is a front end (tools/beeb.py sends the lines).
#ifndef CONSOLE_H
#define CONSOLE_H

// Starts the console task. Commands: help, list, boot <disc>, reset.
void console_start(void);

// The disc chosen at the console, persisted in NVS (key "disc"); an empty
// string when none has been chosen. `out` holds at least 32 bytes.
void console_get_disc(char *out, unsigned len);
// The speaker volume chosen at the console (NVS key "vol"), 0 to 100.
int console_get_volume(void);

#endif
