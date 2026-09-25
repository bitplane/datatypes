# XV thumbnail

Reads XV thumbnails, the `P7 332` files that XV, GIMP 1.x and makexvpics keep in `.xvpics` directories, as 8-bit 3:3:2 RGB.

Saves XV thumbnails at the picture's own size, without dithering or scaling down to 80×60.

The `XVTHUMB` descriptor matches the `P7 332` magic, whatever the file's name.
