// esp32-beeb: the disc menu. A swipe across the picture's short side sets a
// flag in NVS and restarts; the next boot runs this instead of the machine:
// every disc in the catalogue (bundle and card) in the Beeb's own font, one
// text row per panel band.
//
//   tap a disc            highlight it; tap it again to boot it
//   swipe along           next / previous page
//   swipe across          leave, back to the current disc
//   PWR short press       leave (restart), as everywhere
#ifndef MENU_H
#define MENU_H

// Draws the menu and handles touch until a disc is chosen; never returns
// (it restarts the board into the chosen disc).
void menu_run(void);

#endif
