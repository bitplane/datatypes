# Atari ST screens

Reads uncompressed and simply packed Atari ST screen dumps from painting and digitiser programs.

| Program | Extension | Picture |
|---|---|---|
| Art Director, GFA Artist, MonoSTar, The ArtiST | `.sta` | 320×200×16 or 640×400 mono |
| Doodle | `.doo` | 640×400 mono |
| ColorSTar | `.bil` | 320×200×16 |
| Sinbad Slideshow | `.ssb` | 320×200×16 |
| Synthetic Arts | `.srt` | 640×200×4 |
| PaintShop | `.da4` | 640×800 mono |
| Fullscreen Construction Kit | `.kid` | 448×274×16 |
| RGB Intermediate | `.rgb` | 320×200, 4096 colours |
| Dali, uncompressed | `.sd0`–`.sd2` | low, medium or high |
| Paintworks screens, clips, pages | `.sc0`–`.sc2` `.cl0`–`.cl2` `.pg0`–`.pg2` | all three modes, pages double height |
| Graphics Processor | `.pg1`–`.pg3` | all three modes |
| EZ-Art Professional | `.eza` | 320×200×16 |
| ComputerEyes | `.ce1`–`.ce3` | 320×200 or 640×200 RGB, 640×400 grey |

Rename Atari ST `.art` files to `.sta`, because the C64 class claims `.art`.

Saves uncompressed Paintworks screens and pages. The picture must fit the mode's palette exactly.

Not supported: compressed Dali, which the ST compressed paint class reads, multi-palette pictures, ColorSTar objects and pictures with separate palette files.

Our `STSCREEN` descriptor matches the extensions above on files of at least 32 bytes, at priority -10. SGI `.rgb` files still go to the SGI class by their magic.
