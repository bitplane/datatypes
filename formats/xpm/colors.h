#ifndef BITPLANE_XPM_COLORS_H
#define BITPLANE_XPM_COLORS_H
#include <stddef.h>
#include <stdint.h>
/* Parse an XPM colour value: "None" (transparent, all zero), "#" with 3, 6, 9
   or 12 hex digits, or an X11 colour name. Names ignore case and spaces, as
   the X server's lookup does. Returns 0 for anything else. */
int xpm_parse_color(const uint8_t *text, size_t length, uint8_t rgba[4]);
#endif
