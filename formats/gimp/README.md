# GIMP

Reads GIMP's own files:

| File | Reads | Saves |
|---|---|---|
| Brush `.gbr` `.gpb` | Grey (black on white, as in GIMP), RGBA, version 1, GIMP 1.x pixmap | Grey if opaque grey, else RGBA |
| Pipe `.gih` | Each cell: the first, or the one `PDTA_WhichPicture` names | One cell |
| Pattern `.pat` | Grey, grey with alpha, RGB, RGBA | The smallest that fits |
| Image `.xcf` | Up to GIMP 3.2, composited as GIMP shows it: groups, masks, all layer modes, floating selections; 8 to 32-bit integer, grey, indexed | One RGB or RGBA layer |

Not supported: CinePaint float brushes, Photoshop patterns, floating-point XCF and visible non-destructive filters. Colour profiles are ignored.

`GIMP_GBR` and `GIMP_PAT` match the magic at offset 20 in `.gbr`, `.gpb` and `.pat` files; `GIMP_XCF` matches `gimp xcf` in any file. Version 1 brushes and pipes have no magic, so `GIMP_GBR1` and `GIMP_GIH` match `.gbr` and `.gih` names at priority -10.
