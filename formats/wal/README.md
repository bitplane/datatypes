# Quake 2 WAL

Reads Quake 2 wall textures (`.wal`), as written by qdata, Wally, TexMex and the other Quake 2 tools, and used by Quake 2 engine games that kept the stock format, such as Kingpin. Pixels index the Quake 2 palette from `pics/colormap.pcx` and load opaque, index 255 included.

Each file holds four mip levels. The full-size texture loads by default, and `PDTA_WhichPicture` picks a smaller level.

Not supported: Daikatana's WAL with its own palette, Heretic II `.m8`/`.m32`, SiN `.swl` and Quake 1 miptex.

Saves pictures whose colours are all in the Quake 2 palette, after compositing over white; anything else fails. The mip levels are averaged and matched to the nearest palette colour.

WAL has no magic, so our `WAL` descriptor matches the zero high bytes of the width, height and first offset on files named `.wal`, at priority -10.
