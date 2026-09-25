# CMU Window Manager

Reads CMU window manager (Andrew Toolkit) bitmaps in either byte order, with a 14 or 16-byte header.

Saves the big-endian 14-byte form, as netpbm and the Andrew Toolkit write it.

Our `CMUWM` descriptor matches the big-endian magic `F1 00 40 BB`. The decoder also reads little-endian files, magic `BB 40 00 F1`, but the descriptor doesn't recognise them.
