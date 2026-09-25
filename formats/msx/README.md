# MSX screens

Reads MSX, MSX2 and MSX2+ screens saved by BASIC's `BSAVE` or Graph Saurus.

| Screen | Extensions |
|---|---|
| 2–4 | `.sc2` `.grp` `.sc3` `.sc4` |
| 5–7 | `.sc5` `.ge5` `.sc6` `.sc7` `.ge7` |
| 8 | `.sc8` `.ge8` `.sr8` |
| 10, 12 (YJK) | `.sca` `.scb` `.scc` `.srs` `.yjk` |
| Graph Saurus | `.sr5` `.sr6` `.sr7` `.sri`, palette in `.pl5`–`.pl7` |

Missing palettes default to the MSX2's, or the TMS9918's for screens 2 and 3. Whole 16K or 64K dumps show their sprites.

Saves 256×212 as screen 5 or 8, and 512×212 as screen 7. Colours must be exact MSX levels.

Not supported: interlaced `.s1?` pairs, `.gl?` and `.sh?` blocks, and `.pic`.

`MSX` matches a `BSAVE` header at priority -9, leaving Paintworks `.sc2` to the ST screens class. `MSX_RAW` matches packed and headerless Graph Saurus pictures by name at priority -10.
