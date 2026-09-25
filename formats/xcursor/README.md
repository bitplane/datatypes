# Xcursor

Reads X11 cursor files, as shipped in Linux desktop cursor themes. Images are 32-bit ARGB and keep their alpha channel. A file holds several images, in different sizes and animation frames. The largest loads by default, and `PDTA_WhichPicture` picks another. Hotspots and frame delays are ignored.

Saves a one-image cursor with its hotspot at the top left.

Our `XCURSOR` descriptor matches the `Xcur` magic whatever the file is called, since theme cursors have no extension.
