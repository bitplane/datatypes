# Atari ST compressed paint

Reads the compressed pictures of eight Atari ST paint programs. All but PaintShop give a 320×200 16-colour, 640×200 4-colour or 640×400 mono screen.

| Program | Extension | Notes |
|---|---|---|
| Tiny | `.tny` `.tn1`–`.tn6` | colour cycling ignored |
| CrackArt | `.ca1`–`.ca3` | |
| Imagic | `.ic1`–`.ic3` | |
| STAD | `.pac` | |
| Dali, compressed | `.lpk` `.mpk` `.hpk` | |
| Pablo Paint | `.ppp` `.pa3` | |
| Picworks | `.cp3` | |
| PaintShop | `.psc` | up to 640×400 |

Saves Tiny.

Not supported: Pablo's compressed variant and uncompressed Imagic. Uncompressed Dali and Paintworks belong to the ST screens class.

We ship two descriptors. `STPAINT` matches the extensions above by name at priority -10. `STPAINT_PAC` matches `.pac` files that start with `pM8` at priority -9, so Bohemia PAA `.pac` files still reach the PAA class.
