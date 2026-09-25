# XPM

Reads X11 pixmaps in XPM3, XPM2 plain and C forms, and XPM1, with up to 32 characters per pixel. Colours can be `None` for transparent, `#` hex values, or X11 colour names from X.Org's `rgb.txt`. The hotspot becomes the picture's grab point.

Saves XPM3 with `#RRGGBB` colours. Pixels with alpha below 128 become `None`. The array takes its name from the file, and a non-zero grab point becomes the hotspot.

Not supported: files without the XPM comment, XPM2 in Lisp syntax, `rgb:` and other Xcms colour specifications, and names missing from `rgb.txt`.

The three XPM versions start with different bytes, so our `XPM` descriptor matches files named `.xpm` or `.xpm2`.
