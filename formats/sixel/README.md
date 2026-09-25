# SIXEL

Reads DEC SIXEL terminal graphics, as libsixel, ImageMagick, netpbm and gnuplot write them. Terminal captures with text before the image load too. Colours can be RGB or HLS. A file can hold several images, such as animation frames. The first loads by default, and `PDTA_WhichPicture` picks another.

Saves one image with RGB colours, reduced to 256 colours if it has more. Pixels with alpha below 128 are left undrawn.

Our `SIXEL` descriptor matches ASCII files that start with ESC and are named `.six` or `.sixel`. The decoder also reads files that use the 8-bit `0x90` introducer, but AROS treats them as binary, so the descriptor doesn't recognise them.
