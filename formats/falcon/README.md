# Atari Falcon and TT

Reads the paint formats of the Atari Falcon and TT.

| Program | Extension | Picture |
|---|---|---|
| GodPaint | `.god` | RGB565 |
| IndyPaint | `.tru` | RGB565 |
| EggPaint, Spooky Sprites | `.trp` | RGB565 |
| COKE | `.tg1` | RGB565 |
| Rembrandt | `.tcp` | RGB565, several pictures |
| Falcon screen dump | `.ftc` | 384×240 RGB565 |
| Prism Paint, TruePaint | `.pnt` `.tpi` | 2 to 256 colours, 16 or 24-bit |
| DuneGraph | `.dg1` `.dc1` | 256 colours |
| Fuckpaint | `.pi4` `.pi7` `.pi9` | 256 colours |
| DEGAS, TT modes | `.pi4` `.pi5` `.pi6` | 320×480×256, 640×480×16, 1280×960 mono |

Pixels load as stored, so TT low resolution pictures look squeezed. A Rembrandt file loads its first picture by default, and `PDTA_WhichPicture` picks another.

Saves 24-bit Prism Paint.

Not supported: compressed Rembrandt, ICE-packed EggPaint, and 320×240 ST pictures in DEGAS files.

Our `FALCON` descriptor matches the extensions above by name at priority -11, below MacPaint, which checks `.pnt` files first.
