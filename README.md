# AROS image datatypes

Picture datatype classes for AROS. Each format is built, versioned and packaged separately.

## Targa

Reads colour-mapped, true-colour and grayscale TGA files, including RLE images. Saves RLE true-colour TGA: 24-bit for opaque images and 32-bit when pixels have transparency. AROS's `Devs/DataTypes/Targa` descriptor selects the class.

## PCX

Reads packed and planar indexed PCX, 8-bit indexed PCX, and 24-bit RGB PCX. Saves 24-bit RGB PCX, compositing transparency over white. AROS's `Devs/DataTypes/PCX` descriptor selects the class.

## DCX

Reads DCX (multi-page PCX) files: a directory of up to 1024 page offsets, each pointing to a PCX image. Pages can be 1, 2 or 4-bit packed, 1-bit planar with 2 to 4 planes, 8-bit indexed or gray, 24-bit RGB, or 32-bit RGBA with its alpha kept. Each 8-bit page uses the palette at the end of that page. Odd bytes per line, as ImageMagick and netpbm write them, are accepted. 1-bit pages whose two palette colours are equal load black and white, and pages without a palette use netpbm's default colours. `PDTA_WhichPicture` picks a page in directory order, the first by default, and `PDTA_GetNumPictures` returns the page count. Saves a one-page DCX holding a 24-bit RGB PCX, compositing transparency over white, with the full 1024-entry directory ImageMagick writes. Other depth and plane combinations, such as 16-bit or 2-bit planar pages, are rejected.
The package includes its `Devs/DataTypes/DCX` descriptor, which recognises files by their `B1 68 DE 3A` magic whatever their name. `formats/dcx/DCX.dtyp` is the compiled form of `DCX.dtd`; regenerate it with AROS's `createdtdesc -o formats/dcx/DCX.dtyp formats/dcx/DCX.dtd` if the recognition rules change.

## QOI

Reads and saves QOI RGB and RGBA images. The package includes its `Devs/DataTypes/QOI` descriptor.
`formats/qoi/QOI.dtyp` is the compiled form of `QOI.dtd`; regenerate it with AROS's `createdtdesc -o formats/qoi/QOI.dtyp formats/qoi/QOI.dtd` if the recognition rules change.

## SGI

Reads SGI (IRIS) images, verbatim or RLE, with 8 or 16 bits per channel: grayscale, gray with alpha, RGB and RGBA. 16-bit channels keep their top byte, and channels beyond the fourth are ignored. Also reads the obsolete dithered (3-3-2 RGB) images, and screen images as grayscale, since their palette isn't stored in the file. Saves 8-bit RLE: grayscale when every pixel is gray, RGB otherwise, and RGBA when pixels have transparency.
The package includes its `Devs/DataTypes/SGI` descriptor, which matches the `01 DA` magic on files named `.rgb`, `.rgba`, `.bw`, `.sgi`, `.int` or `.inta`. `formats/sgi/SGI.dtyp` is the compiled form of `SGI.dtd`; regenerate it with AROS's `createdtdesc -o formats/sgi/SGI.dtyp formats/sgi/SGI.dtd` if the recognition rules change.

## WBMP

Reads type 0 WBMP (WAP bitmap), the 1-bit black and white format. Saves type 0 WBMP: pixels are composited over white, then set white if their luminance is at least half. The package includes its `Devs/DataTypes/WBMP` descriptor. WBMP has no magic number, so the descriptor matches the two leading zero bytes and requires a `.wbmp` name, at priority -10.
`formats/wbmp/WBMP.dtyp` is the compiled form of `WBMP.dtd`; regenerate it with AROS's `createdtdesc -o formats/wbmp/WBMP.dtyp formats/wbmp/WBMP.dtd` if the recognition rules change.
## Sun Raster

Reads 1-bit, 8-bit, 24-bit and 32-bit Sun Raster files in old, standard, RLE and RGB types, with or without an RGB colormap. 1-bit images without a colormap are black on white and 8-bit ones are grayscale. The pad byte of 32-bit pixels is ignored, so images load opaque. Saves uncompressed 24-bit Sun Raster, compositing transparency over white. The package includes its `Devs/DataTypes/SUNRASTER` descriptor, which recognises files by their magic number whatever their name.
`formats/sunraster/SUNRASTER.dtyp` is the compiled form of `SUNRASTER.dtd`; regenerate it with `createdtdesc -o formats/sunraster/SUNRASTER.dtyp formats/sunraster/SUNRASTER.dtd`. `createdtdesc` also builds on the host from AROS's `tools/dtdesc`.

## XBM

Reads X11 bitmaps (`char` arrays) and older X10 bitmaps (`short` arrays), accepting what Xlib's `XReadBitmapFile` accepts: comments, other preprocessor lines, decimal or negative values, and names that don't share a prefix. Images load as one-plane pictures, with set bits in black on an opaque white background. A hotspot becomes the picture's grab point. Saves X11 bitmaps: dark pixels (by luminance, after compositing over white) become set bits, the C names come from the file name being written, and a non-zero grab point is written as the hotspot. The package includes its `Devs/DataTypes/XBM` descriptor, which matches files named `#?.xbm`, because XBM files have no fixed header to look for.
`formats/xbm/XBM.dtyp` is the compiled form of `XBM.dtd`; regenerate it with AROS's `createdtdesc -o formats/xbm/XBM.dtyp formats/xbm/XBM.dtd` if the recognition rules change.

## Farbfeld

Reads Farbfeld images: 16-bit big-endian RGBA with straight alpha. Channels are rounded to the nearest 8-bit value, including images whose alpha is zero everywhere. Bytes after the last pixel are ignored. Saves Farbfeld with each 8-bit channel widened exactly (times 257), so a saved image loads back unchanged. The package includes its `Devs/DataTypes/FARBFELD` descriptor, which recognises files by their `farbfeld` magic whatever their name.
`formats/farbfeld/FARBFELD.dtyp` is the compiled form of `FARBFELD.dtd`; regenerate it with AROS's `createdtdesc -o formats/farbfeld/FARBFELD.dtyp formats/farbfeld/FARBFELD.dtd` if the recognition rules change.
## MacPaint

Reads MacPaint files, versions 0, 2 and 3, with or without a 128-byte MacBinary header in front, as 576×720 one-plane pictures in black on white. The version field and pattern table are ignored, as are padding and resource forks after the image. PackBits runs may cross rows, a `0x80` flag byte repeats the next byte 129 times as ImageMagick and netpbm read it, and a run past the last pixel is clipped. Files that end before the last row are rejected. Saves version 0 MacPaint with each row packed on its own: dark pixels (by luminance, after compositing over white) become black, smaller pictures are padded with white, and larger ones keep only their top-left 576×720. The package includes its `Devs/DataTypes/MACPAINT` descriptor. MacPaint has no magic number, so the descriptor matches the leading zero byte that both plain and MacBinary files start with, and requires a `#?.(mac|macp|pntg|pnt)` name, at priority -10. MacBinary files named `.bin` aren't recognised.
`formats/macpaint/MACPAINT.dtyp` is the compiled form of `MACPAINT.dtd`; regenerate it with AROS's `createdtdesc -o formats/macpaint/MACPAINT.dtyp formats/macpaint/MACPAINT.dtd` if the recognition rules change.

## XPM

Reads X11 pixmaps: XPM3 (`/* XPM */`), XPM2 in its plain (`! XPM2`) and C (`/* XPM2 C */`) forms, and XPM1 (`#define name_format`), with up to 32 characters per pixel. Each colour takes the first of its `c`, `g`, `g4` and `m` keys that parses, and ignores `s` (symbolic) names. A colour can be `None` (transparent), `#` with 3, 6, 9 or 12 hex digits (rounded to 8 bits per channel), or an X11 colour name. Names match regardless of case and spaces and take their values from X.Org's `rgb.txt`. The hotspot becomes the picture's grab point, and the class skips `XPMEXT` extensions. It rejects files without the XPM comment, XPM2 in Lisp syntax, `rgb:` and other Xcms colour specifications, and names missing from `rgb.txt`.
Saves XPM3 with `#RRGGBB` colours, using as few characters per pixel as the colour count allows. Pixels with alpha below 128 become `None`; the rest are composited over white. The array takes its name from the file, and a non-zero grab point becomes the hotspot.
The package includes its `Devs/DataTypes/XPM` descriptor. The three XPM versions start with different bytes, so the descriptor matches the file name (`#?.xpm` or `#?.xpm2`) rather than a header. `formats/xpm/XPM.dtyp` is the compiled form of `XPM.dtd`; regenerate it with AROS's `createdtdesc -o formats/xpm/XPM.dtyp formats/xpm/XPM.dtd` if the recognition rules change.

## PAM and PFM

Reads Netpbm PAM (`P7`) and the float maps PFM (`PF` colour, `Pf` gray, `PF4` RGBA) and PHM (`PH`, `Ph`, half floats). PAM covers the tuple types `BLACKANDWHITE`, `GRAYSCALE`, `RGB` and `CMYK`, each with or without `_ALPHA`, at any maxval up to 65535. Samples scale to 8 bits with netpbm's rounding, and values above maxval are clamped. Without a known tuple type, one or two planes load as gray and three or more as RGB. Alpha is only used when the tuple type declares it, including alpha that is zero everywhere. Float maps are linear light: samples are clamped to 0–1 and encoded with the sRGB curve, and alpha is kept linear. The sign of the scale gives the byte order. Its size is ignored. A file can hold several images, one after another (PAM and float maps may be mixed). `PDTA_WhichPicture` picks one, and `PDTA_GetNumPictures` reports how many there are. Saves 8-bit PAM as `GRAYSCALE` when every pixel is gray and `RGB` otherwise, adding alpha (`_ALPHA`) when pixels have transparency.
Not supported: PNM images (`P1`–`P6`, left to AROS's `pnm` class, including inside a PAM stream), XV thumbnails (`P7 332`), and float map headers with comments or CRLF line ends, which the reference tools also reject or misread.
The package includes its `Devs/DataTypes/PAM` descriptor. It matches `P` followed by two bytes, at priority -1, so AROS's PNM descriptors (priority 0) still take `P1`–`P6`. This also recognises `PF4`; unrelated matches are rejected by the decoder. `formats/pam/PAM.dtyp` is the compiled form of `PAM.dtd`; regenerate it with AROS's `createdtdesc -o formats/pam/PAM.dtyp formats/pam/PAM.dtd` if the recognition rules change.

## XWD

Reads X11 window dumps (version 7) in every visual class. ZPixmap images can have 1, 4, 8, 16, 24 or 32 bits per pixel, XYBitmap images have depth 1, and XYPixmap images can be any depth up to 32. Pixel data follows the header's byte order, bit order and scanline unit, as Xlib reads it. The header itself may be big-endian, as `xwd` writes it, or little-endian. Colours come from the file's colormap when it has one: palette visuals index it by pixel, and TrueColor and DirectColor index it per channel. Without a colormap, TrueColor and DirectColor scale each field by its mask, and gray visuals use a ramp in which 1-bit pixels are 0 white, 1 black. XWD has no alpha, so images load opaque. X10 dumps (version 6) and 2 or 12 bits per pixel are not supported.
Saves 24-bit TrueColor in 32-bit big-endian pixels with no colormap, the layout an X server's own dumps use, compositing transparency over white. The package includes its `Devs/DataTypes/XWD` descriptor. It matches files named `#?.xwd`; the decoder validates the header and accepts either byte order.
`formats/xwd/XWD.dtyp` is the compiled form of `XWD.dtd`; regenerate it with AROS's `createdtdesc -o formats/xwd/XWD.dtyp formats/xwd/XWD.dtd` if the recognition rules change.

## Palm bitmap

Reads Palm OS bitmaps, versions 0 to 3: 1, 2, 4 and 8-bit indexed, and 16-bit RGB565 direct colour, uncompressed or with scanline, RLE or PackBits compression. Indexed bitmaps use their colour table when they have one; otherwise 1, 2 and 4-bit bitmaps are gray from white to black and 8-bit ones use the Palm system palette. ImageMagick flags its 1, 2 and 4-bit bitmaps as having a colour table without writing one, so at those depths the flag counts only when a table of at most 2^depth entries is actually there. The transparent index or colour becomes transparent when the header flags it, even if it covers the whole image. A file holding a bitmap family (several depths or densities chained together, with or without the high-density separator) is a multi-image picture: `PDTA_WhichPicture` picks a bitmap in file order, `PDTA_GetNumPictures` reports how many there are, and by default the largest, then deepest, bitmap loads. Little-endian (`indexedLE`, `rgb565LE`) version 3 bitmaps and direct colour other than 5:6:5 are rejected.
Saves an uncompressed 8-bit bitmap with a colour table when the image has at most 256 colours, which is lossless, and a 16-bit RGB565 bitmap otherwise. Fully transparent pixels become the transparent colour; partly transparent ones are composited over white.
The package includes its `Devs/DataTypes/PALM` descriptor. Palm bitmaps have no magic number, so it matches files named `#?.palm` of at least 16 bytes, at priority -10. `formats/palm/PALM.dtyp` is the compiled form of `PALM.dtd`; regenerate it with AROS's `createdtdesc -o formats/palm/PALM.dtyp formats/palm/PALM.dtd` if the recognition rules change.
## OTB

Reads Nokia OTA bitmaps (`.otb`), the 1-bit format of operator logos and picture messages, with set bits in black. Handles 8-bit and 16-bit sizes and skips extension fields. It ignores the external palette flag and any bytes after the image. Animated bitmaps hold up to 16 pictures: `PDTA_WhichPicture` selects one, the first by default, and `PDTA_GetNumPictures` reports how many there are. The specification packs rows without padding, but ImageMagick pads each row to a byte. The class reads a file as padded when it is long enough to be, and as packed otherwise. Widths that are a multiple of 8, including the usual 72, are the same either way. Saves one-picture OTB with rows padded to a byte, as ImageMagick reads it, and 8-bit sizes when both fit. Pixels are composited over white, then set black if their luminance is below half. Doesn't read compressed bitmaps (the specification never defined the scheme), more than one colour plane, or OTA bitmaps stored as hex text. The package includes its `Devs/DataTypes/OTB` descriptor. OTB has no magic number, so the descriptor requires a `.otb` name and has priority -10.
`formats/otb/OTB.dtyp` is the compiled form of `OTB.dtd`; regenerate it with AROS's `createdtdesc -o formats/otb/OTB.dtyp formats/otb/OTB.dtd` if the recognition rules change.

## MSP

Reads Microsoft Paint images from Windows 1 (`DanM`, uncompressed) and Windows 2 (`LinS`, run-length encoded by row). Images load as one-plane pictures, black and white. The header checksum isn't checked. In version 2 files, a row whose packed size is zero, or whose runs stop short, is white to its end, and a run past the end of its row is cut off. Saves version 1 files: pixels are composited over white, then set white if their luminance is at least half. The package includes its `Devs/DataTypes/MSP` descriptor. The two versions' keys share only their third byte, `n`, so the descriptor matches that byte and requires a `.msp` name.
`formats/msp/MSP.dtyp` is the compiled form of `MSP.dtd`; regenerate it with AROS's `createdtdesc -o formats/msp/MSP.dtyp formats/msp/MSP.dtd` if the recognition rules change.

## NEOchrome

Reads Atari ST NEOchrome `.neo` pictures in all three screen modes: low resolution (320×200, 16 colours), medium (640×200, 4 colours) and high (640×400, black on white). ST palettes use 3 bits per gun, scaled as netpbm scales them. A palette counts as STE, with 4 bits per gun, when one of the colours the mode uses has a fourth bit set; colours the mode doesn't use often hold junk, so they are ignored. Colour-cycling data and bytes after the image are ignored. Files whose flag word isn't zero or whose resolution isn't 0–2 are rejected. Saves NEOchrome when the picture is one of the three screen sizes and, after compositing over white, fits that mode's palette exactly with ST or STE levels. Other pictures can't be saved, because the format can't hold them without loss. The package includes its `Devs/DataTypes/NEO` descriptor. NEOchrome has no magic number, so the descriptor matches the zero flag word and the high byte of the resolution, requires a `.neo` name, and has priority -10.
`formats/neo/NEO.dtyp` is the compiled form of `NEO.dtd`; regenerate it with AROS's `createdtdesc -o formats/neo/NEO.dtyp formats/neo/NEO.dtd` if the recognition rules change.

## XV thumbnail

Reads XV thumbnails, the `P7 332` files XV, GIMP 1.x and makexvpics keep in `.xvpics` directories: 8-bit 3:3:2 RGB, expanded as `v * 255 / max` rounded down, as Pillow and netpbm do. Comment lines are skipped with or without `#END_OF_COMMENTS`, and the size line's maxval may be left out, but when present it must be 255. Saves XV thumbnails at the image's own size: pixels are composited over white, then each channel takes the nearest of the eight (or four, for blue) levels, as netpbm's `pamtoxvmini` does. There is no dithering, and the image is not scaled down to XV's 80×60. The package includes its `Devs/DataTypes/XVTHUMB` descriptor, which recognises files by their `P7 332` magic whatever their name.
`formats/xvthumb/XVTHUMB.dtyp` is the compiled form of `XVTHUMB.dtd`; regenerate it with AROS's `createdtdesc -o formats/xvthumb/XVTHUMB.dtyp formats/xvthumb/XVTHUMB.dtd` if the recognition rules change.

## TIM

Reads PlayStation TIM textures: 4-bit and 8-bit indexed, 16-bit 5:5:5 and 24-bit RGB. Indexed images use the first palette of their CLUT; entries missing from a short CLUT are black, and images without a CLUT (whose palette lives elsewhere in VRAM) load as grayscale. A CLUT in a 16-bit or 24-bit file is skipped. Images load opaque: the STP bit and transparent black are rendering modes of the PlayStation GPU, not alpha stored in the file. Several TIMs stored back to back in one file are separate pictures, selected with `PDTA_WhichPicture`. Mixed-mode (frame buffer) TIMs are not supported. Saves 24-bit TIM, compositing transparency over white. The package includes its `Devs/DataTypes/TIM` descriptor, which matches the `10 00 00 00` ID and the zero reserved flag bytes on files named `.tim`.
`formats/tim/TIM.dtyp` is the compiled form of `TIM.dtd`; regenerate it with AROS's `createdtdesc -o formats/tim/TIM.dtyp formats/tim/TIM.dtd` if the recognition rules change.

## ICNS

Reads Apple icon files: PNG entries in any PNG colour type and bit depth, interlaced or not; 24-bit icons, packed or uncompressed, with their 8-bit masks (`is32`, `il32`, `ih32`, `it32`, and `icp4`/`icp5` holding the same data); ARGB entries (`ic04`, `ic05`, `icsb`); and classic 1-, 4- and 8-bit icons with their 1-bit masks, in the Mac OS system palettes. JPEG 2000 entries are unsupported and skipped, so a file holding only JPEG 2000 doesn't load. Nested icon sets (dark mode, template, selected) are ignored. 16-bit PNG channels are rounded to 8 bits, and explicit alpha is preserved even when it is zero everywhere. PNG data is inflated by `z1.library`.
A file holds several images. `PDTA_WhichPicture` picks one by its position among the loadable entries, in file order, and `PDTA_GetNumPictures` reports how many there are. Otherwise the largest image loads, then the deepest, and a 1x entry is preferred to a 2x entry of the same pixel size.
Saves a one-image ICNS file when the picture is square: 24-bit packed with an 8-bit mask at 16, 32, 48 and 128 pixels, and PNG at 64, 256, 512 and 1024 pixels. Other sizes can't be saved. The package includes its `Devs/DataTypes/ICNS` descriptor, which matches the `icns` magic on files named `#?.icns`.
`formats/icns/ICNS.dtyp` is the compiled form of `ICNS.dtd`; regenerate it with AROS's `createdtdesc -o formats/icns/ICNS.dtyp formats/icns/ICNS.dtd` if the recognition rules change.

## MGR

Reads 1-bit bitmaps from the MGR window system, with set bits in black: the current `yz` layout with depth 1 and rows padded to 8 bits, and the older `zz` and `xz` layouts padded to 16 and 32 bits. Sides are up to 4095, as the header encodes them. Bytes after the image are ignored. Saves `yz` with depth 1, as MGR and netpbm write it. Pixels are composited over white, then set black if their luminance is below half. Doesn't read colour MGR pixmaps (`yz` with depth 8, or `zy`). They index the MGR server's palette, which the file doesn't carry, and netpbm rejects them too. The package includes its `Devs/DataTypes/MGR` descriptor. MGR files usually have no extension, so it matches on content only: `yz`, then the depth byte for 1 (`!`), at priority -1. The decoder reads old `zz` and `xz` files, but the descriptor doesn't recognise them, so MultiView won't open them yet: a descriptor has one mask, and their two-byte magic is too weak to match on alone. Only one of the 367 files in MGR 0.69 uses an old layout.
`formats/mgr/MGR.dtyp` is the compiled form of `MGR.dtd`; regenerate it with AROS's `createdtdesc -o formats/mgr/MGR.dtyp formats/mgr/MGR.dtd` if the recognition rules change.

## CMU Window Manager

Reads CMU window manager (Andrew Toolkit) bitmaps, where a clear bit is black and rows are padded to a byte. Both byte orders load: the magic `F1 00 40 BB` means big-endian, as netpbm reads it, and `BB 40 00 F1` means little-endian. As in the Andrew Toolkit's reader, the header is 14 bytes, or 16 when the file has at least two bytes more than a 14-byte header needs. The depth must be 1, read as 16 bits, or as 32 bits in a 16-byte header. Saves the big-endian 14-byte form, as netpbm and the Andrew Toolkit write it. Pixels are composited over white, then set black if their luminance is below half. The package includes its `Devs/DataTypes/CMUWM` descriptor. It matches the big-endian magic at priority 0. The decoder reads little-endian files, but the descriptor doesn't recognise them, because a package ships one descriptor with one mask.
`formats/cmuwm/CMUWM.dtyp` is the compiled form of `CMUWM.dtd`; regenerate it with AROS's `createdtdesc -o formats/cmuwm/CMUWM.dtyp formats/cmuwm/CMUWM.dtd` if the recognition rules change.
## FAX

Reads raw CCITT Group 3 fax files with one-dimensional (MH) coding, and CALS type 1 rasters, whose 2048-byte text header is followed by Group 4 (T.6) data. Images load as one-plane pictures, black on white.

- **Group 3:** bits may be stored first-to-last (as T.4 sends them) or last-to-first (as many modems save them); the loader works out which. The image is as wide as its longest line, and shorter lines are padded with white. Anything before the first EOL is skipped, the page ends at RTC or at the end of the data, and an empty line mid-page is a white row. Like `g3topbm`, a line with a bad code keeps the part that decoded and the page carries on from the next EOL, so received faxes with line noise still load. Only the first page of a file with several is shown.
- **CALS:** `rpelcnt` gives the size and `rorient` the orientation, which is applied as MIL-PRF-28002 describes it: the pel path and line progression angles, counter-clockwise.
- **Not supported:** Group 3 two-dimensional (MR) coding, raw Group 4 files (they don't record their width), CALS type 2 (tiled) rasters, and the Digifax header some fax software adds.

Saves raw Group 3 MH at the picture's own width, bits first-to-last, with an EOL before each line and RTC at the end. Pixels are composited over white, then set black if their luminance is under half. Fax machines expect 1728-pixel lines, so pad the picture to that width first if the file is to be sent.

The package includes its `Devs/DataTypes/FAX` descriptor. Raw G3 has no magic number and CALS starts with text, so the one descriptor matches files named `.g3`, `.fax`, `.cal`, `.cals` or `.ct1`, at priority -10.
`formats/fax/FAX.dtyp` is the compiled form of `FAX.dtd`; regenerate it with AROS's `createdtdesc -o formats/fax/FAX.dtyp formats/fax/FAX.dtd` if the recognition rules change.

## ZX Spectrum screen

Reads ZX Spectrum `SCREEN$` dumps: exactly 6912 bytes, the 6144-byte bitmap in the Spectrum's interleaved row order followed by 768 attribute bytes, one per 8×8 cell. They load as 256×192 pictures without the border. Colours use ImageMagick's levels, 0xC0 for normal and 0xFF for bright. Flashing cells show their first phase, ink on paper. Files shorter than 6912 bytes are truncated. Longer ones are other screen formats and are rejected. ImageMagick shows the first 6912 bytes of those, which gives the wrong picture. Not supported: ULA+ palettes (6976 bytes), Timex hi-colour and hi-res screens (12288 and 12289 bytes), two-screen Gigascreen or multicolour files, bitmap-only 6144-byte files and `+3DOS` headers.
Saves a screen when the picture is 256×192 and, after compositing over white, uses only Spectrum colours with at most two per cell, both normal or both bright (black goes with either). Other pictures can't be saved, because the format can't hold them without loss. In a cell with two colours, the one with the lower colour number is paper.
The package includes its `Devs/DataTypes/ZXSCR` descriptor. Screens have no magic number and any 6912 bytes are a valid screen, so the descriptor only requires 32 bytes of data and a `.scr` name, at priority -10. Windows screen savers also use `.scr`. The descriptor claims them too, but the loader rejects them because of their size.
`formats/zxscr/ZXSCR.dtyp` is the compiled form of `ZXSCR.dtd`; regenerate it with AROS's `createdtdesc -o formats/zxscr/ZXSCR.dtyp formats/zxscr/ZXSCR.dtd` if the recognition rules change.

## Build and test

```sh
make test
make build TARGET=x86_64-aros PACKAGE=targa
```

Each module is written to `dist/<target>/<package>.datatype` and installs at `Classes/DataTypes/<package>.datatype`.

## Versions

Each datatype has its own two-number module and package version. Release tags identify one format, for example `targa-1.0`; changes to another format do not change Targa's version.
