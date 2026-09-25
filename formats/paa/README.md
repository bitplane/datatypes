# PAA

Reads Bohemia Interactive PAA and PAC textures from Operation Flashpoint, Arma and DayZ: DXT1 to DXT5, ARGB4444, ARGB1555, ARGB8888 and 8-bit grey with alpha, compressed or not. It also reads Operation Flashpoint's 8-bit palette files, including the 1997 demo's.

Each mip level is a picture, largest first. The first loads by default, and `PDTA_WhichPicture` picks another.

Saves one ARGB8888 level, LZSS-compressed, without smaller mip levels.

Our `PAA` descriptor matches `.paa` and `.pac` files by name at priority -10. The Atari ST compressed paint class claims `.pac` files that start with `pM8`, and the rest come here.
