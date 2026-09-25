# AROS image datatypes

Picture datatype classes for AROS, one per format. Each is built, versioned and released on its own. Follow a format's link for what it reads, what it saves and how its files are recognised.

In the Descriptor column, "AROS" means AROS already ships the `Devs/DataTypes` descriptor, and "ours" means the package installs one.

| Format | Extensions | Saves | Descriptor |
|---|---|---|---|
| [Alias/Wavefront RLA and PIX](formats/alias/) | `.rla` `.pix` `.als` `.alias` | 8-bit RLA | ours |
| [Amiga icons](formats/info/) | `.info` | Project icon | ours |
| [Atari Falcon and TT](formats/falcon/) | `.god` `.tru` `.trp` `.tcp` `.pnt` `.tpi` `.dc1` `.pi4` and more | 24-bit Prism Paint | ours |
| [Atari ST compressed paint](formats/stpaint/) | `.tny` `.ca1` `.ic1` `.pac` `.lpk` `.ppp` `.cp3` `.psc` and more | Tiny | ours |
| [Atari ST screens](formats/stscreen/) | `.sta` `.doo` `.sc0` `.pg1` `.sd0` `.eza` `.ce1` and more | Paintworks | ours |
| [AVS and AAI](formats/avs/) | `.avs` `.x` `.aai` | AVS or AAI | ours |
| [BLP](formats/blp/) | `.blp` | BLP2, one level | ours |
| [C64 pictures](formats/c64/) | `.koa` `.art` `.dd` `.fli` `.drl` and more | Koala or Art Studio | ours |
| [CMU Window Manager](formats/cmuwm/) | none | Big-endian 1-bit bitmap | ours |
| [CompuServe RLE](formats/cis/) | `.rle` `.cis` | 128×96 or 256×192 | ours |
| [DCX](formats/dcx/) | `.dcx` | One-page 24-bit RGB | ours |
| [DDS](formats/dds/) | `.dds` | 24-bit RGB or 32-bit ARGB | ours |
| [Farbfeld](formats/farbfeld/) | `.ff` | 16-bit RGBA | ours |
| [FAX](formats/fax/) | `.g3` `.fax` `.cal` `.cals` `.ct1` `.c4` `.mil` `.ras` | Raw Group 3 MH | ours |
| [FTEX](formats/ftex/) | `.ftc` `.ftu` | Uncompressed 24-bit `.ftu` | ours |
| [ICNS](formats/icns/) | `.icns` | Square icons, one image | ours |
| [ICO](formats/ico/) | `.ico` `.cur` | One-image BMP icon or cursor | ours |
| [Japanese PC pictures](formats/japanpc/) | `.mag` `.max` `.mki` `.pi` `.pic` | MAG | ours |
| [KiSS CEL](formats/kisscel/) | `.cel` | 32-bit cel | ours |
| [Lunapaint](formats/lunapaint/) | none | One layer, one frame | AROS |
| [MacPaint](formats/macpaint/) | `.mac` `.macp` `.pntg` `.pnt` | Version 0 | ours |
| [MGR](formats/mgr/) | none | 1-bit `yz` bitmap | ours |
| [MSP](formats/msp/) | `.msp` | Windows 1 | ours |
| [MTV and QRT](formats/mtv/) | `.mtv` `.pic` `.qrt` `.dis` | One MTV image | ours |
| [NEOchrome](formats/neo/) | `.neo` | ST screens that fit exactly | ours |
| [OTB](formats/otb/) | `.otb` | 1-bit, one picture | ours |
| [PAA](formats/paa/) | `.paa` `.pac` | ARGB8888 | ours |
| [Palm bitmap](formats/palm/) | `.palm` | 8-bit indexed or RGB565 | ours |
| [Palm ImageViewer](formats/pdb/) | `.pdb` | Uncompressed grayscale | ours |
| [PAM and PFM](formats/pam/) | `.pam` `.pfm` `.phm` | 8-bit PAM | ours |
| [PCX](formats/pcx/) | `.pcx` | 24-bit RGB | AROS |
| [Pixar](formats/pixar/) | `.pxr` `.pic` `.picio` `.pixar` | 8-bit RGB or RGBA | ours |
| [PowerVR PVR](formats/pvr/) | `.pvr` | 8-bit RGB or RGBA | ours |
| [QOI](formats/qoi/) | `.qoi` | RGB or RGBA | ours |
| [SGI](formats/sgi/) | `.rgb` `.rgba` `.bw` `.sgi` `.int` `.inta` | 8-bit RLE gray, RGB or RGBA | ours |
| [SIXEL](formats/sixel/) | `.six` `.sixel` | RGB, up to 256 colours | ours |
| [Spectrum 512](formats/spectrum/) | `.spu` `.spc` | SPU, if the colours fit | ours |
| [Sun icon](formats/sunicon/) | none | Depth 1 icon | ours |
| [Sun Raster](formats/sunraster/) | `.ras` | Uncompressed 24-bit | ours |
| [Targa](formats/targa/) | `.tga` | 24 or 32-bit RLE | AROS |
| [TIM](formats/tim/) | `.tim` | 24-bit | ours |
| [TIM2](formats/tim2/) | `.tm2` `.tim2` | 24 or 32-bit | ours |
| [Utah RLE](formats/utahrle/) | `.rle` | Grey, RGB or RGBA | ours |
| [WBMP](formats/wbmp/) | `.wbmp` | Type 0, 1-bit | ours |
| [XBM](formats/xbm/) | `.xbm` | X11 bitmap | ours |
| [Xcursor](formats/xcursor/) | none | One-image cursor | ours |
| [XPM](formats/xpm/) | `.xpm` `.xpm2` | XPM3 | ours |
| [XV thumbnail](formats/xvthumb/) | none | 3:3:2 RGB thumbnail | ours |
| [XWD](formats/xwd/) | `.xwd` | 24-bit TrueColor | ours |
| [ZX Spectrum screen](formats/zxscr/) | `.scr` `.mc` `.ifl` `.bsc` `.img` `.sxg` and more | Standard or Timex hi-colour screen | ours |

## Build and test

```sh
make test
make build TARGET=x86_64-aros PACKAGE=targa
```

Each module is written to `dist/<target>/<package>.datatype` and installs at `Classes/DataTypes/<package>.datatype`.

Packages with their own descriptor keep its source in `formats/<name>/<NAME>.dtd` and the compiled form in `<NAME>.dtyp` beside it. After changing the recognition rules, regenerate it with AROS's `createdtdesc -o formats/<name>/<NAME>.dtyp formats/<name>/<NAME>.dtd`.

## Versions

Each datatype has its own two-number module and package version. Release tags identify one format, for example `targa-1.0`; changes to another format do not change Targa's version.
