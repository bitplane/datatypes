# Spectrum 512

Reads Atari ST Spectrum 512 pictures, uncompressed SPU and compressed SPC, as 320×200 images with 48 colours on every line. A palette with any fourth bit set reads as STE.

Not supported: enhanced `5BIT` SPU files, SPS and SPX.

Saves SPU when the picture is 320×200, its top line is black, every colour is an ST or STE level, and each line's colours fit the 48 palette slots. A very dense picture can fail to save even when a fitting palette exists.

The `SPECTRUM` descriptor matches files named `.spu` or `.spc` at priority -10, since SPU has no magic number. The class rejects files that aren't Spectrum 512.
