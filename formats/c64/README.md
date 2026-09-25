# C64 pictures

Reads Commodore 64 bitmap pictures in the Pepto palette. Pictures are 320×200, with multicolour pixels shown double width. FLI pictures are 296×200, without the three columns the FLI bug hides. Interlaced pictures show the average of their two frames.

| Kind | Programs and extensions |
|---|---|
| Hires | Art Studio `.art` `.aas`, Interpaint `.iph` `.hpi` `.hpc`, Doodle `.dd` `.ddp` `.jj`, Hires-Editor `.het`, Run Paint `.rph` `.rpo`, Hi-Eddi `.hed`, Image System `.ish`, hires bitmaps `.hbm` `.hir`, AFLI `.afl` |
| Multicolour | Koala Painter and copies `.koa` `.kla` `.gig` `.ipt` `.rpm` `.fpt` `.gg`, Amica Paint `.ami`, Advanced Art Studio `.ocp` `.mpi`, Drazpaint `.drz` `.drp`, Blazing Paddles `.pi` `.bpl`, Wigmore Artist `.a64` `.wig`, Rainbow Painter `.rp`, Dolphin Ed and Vidcom `.dol` `.vid` `.vic`, Picasso 64 `.p64`, CDU-Paint `.cdu`, Cheese `.che`, Image System `.ism`, Saracen Paint `.sar`, Paint Magic `.pmg`, Micro Illustrator `.mil` |
| FLI | FLI Graph `.fli`, FLI Designer `.fd2`, Blackmail FLI `.bml` `.flg`, FLI Editor `.fed`, Flimatic `.flm` |
| Interlaced | Drazlace `.drl` `.dlp`, True Paint `.mci`, Fuckpaint `.fp`, Gunpaint `.gun` `.ifl`, Funpaint `.fun` `.fp2`, Flash FLI `.ffli` `.ffl`, Hires Interlace `.hlf` `.hie`, Hireslace `.hle`, ECI `.eci` `.ecp`, Interlace Hires Editor `.ihe`, Vertical Hires Interlace `.vhi` |

Not supported: MUFLI, MUIFLI, NUFLI, UFLI, SHF, Big FLI, Super Hires and other sprite modes, Pixel Perfect, packed Micro Illustrator, True Paint, Blackmail FLI and Flimatic, Hires Manager, character-set and PETSCII screens, sprites, fonts and GoDot.

Saves 320×200 pictures that use only Pepto colours, as Koala Painter if every 4×8 cell fits multicolour limits, otherwise as Art Studio hires if every 8×8 cell has at most two colours. Anything else fails.

The `C64` descriptor matches the extensions above at priority -10, since the files have no magic. Packed formats with no signature (`.gg` `.jj` `.ami` `.ecp`) only load under their own extension.
