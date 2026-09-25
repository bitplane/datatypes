# Atari ST multi-palette

Reads Atari ST pictures that change palette many times per line.

| Program | Extension | Picture |
|---|---|---|
| Multi Palette Picture (MPP, bmp2mpp) | `.mpp` | 320×199 with 46 to 54 colours per line, or 416×273 overscan with 48 |
| PhotoChrome | `.pcs` | 320×199, 48 colours per line |

MPP palettes can be ST (512 colours), STE (4096) or STE with a fifth bit (32768). A PhotoChrome palette with any fourth bit set reads as STE. Pictures made of two alternating screens, meant to flicker into more colours, show as their blend.

Saves nothing.

Spectrum 512 pictures belong to the Spectrum 512 class.

Our `STMULTI` descriptor matches `.mpp` and `.pcs` files by name at priority -10. The class tells the two apart by their headers and rejects other files.
