# TIM

Reads PlayStation TIM textures: 4 and 8-bit indexed, 16-bit 5:5:5 and 24-bit RGB. Indexed images use the first palette of their CLUT, and those without a CLUT load as grayscale. Images load opaque.

A file can hold several TIMs back to back. The first loads by default, and `PDTA_WhichPicture` picks another.

Not supported: mixed-mode frame buffer TIMs.

Saves 24-bit TIM.

The `TIM` descriptor matches the `10 00 00 00` ID and the zero flag bytes on files named `.tim`.
