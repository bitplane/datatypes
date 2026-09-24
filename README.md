# AROS image datatypes

Picture datatype classes for AROS. Each format is built, versioned and packaged separately.

## Targa

Reads colour-mapped, true-colour and grayscale TGA files, including RLE images. Saves RLE true-colour TGA: 24-bit for opaque images and 32-bit when pixels have transparency. AROS's `Devs/DataTypes/Targa` descriptor selects the class.

## PCX

Reads packed and planar indexed PCX, 8-bit indexed PCX, and 24-bit RGB PCX. Saves 24-bit RGB PCX, compositing transparency over white. AROS's `Devs/DataTypes/PCX` descriptor selects the class.

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

## ICO

Reads Windows icons (`.ico`) and cursors (`.cur`). Entries may be BMP, with 1, 4, 8, 16, 24 or 32 bits per pixel, `BI_RGB` or `BI_BITFIELDS`, any header from the 12-byte OS/2 one to V5, or PNG. The AND mask makes pixels transparent, except in 32-bit entries with real alpha. If a 32-bit entry's alpha is zero everywhere, the mask applies instead, as in Windows; an entry with no mask is opaque. PNG entries keep their alpha as it is. A cursor's hotspot becomes the picture's grab point.
Loads the largest entry, and the deepest of equal sizes, unless `PDTA_WhichPicture` picks one by its position in the file. `PDTA_GetNumPictures` reports the number of entries.
AROS's datatypes can only load from files, so a PNG entry is written to a temporary file in `T:` and loaded with the system's PNG datatype, as AROS's AmigaGuide class does for embedded objects. PNG entries therefore need `png.datatype` and a writable `T:`.
Doesn't read RLE, JPEG or PNG compression inside BMP entries, or top-down BMP entries.
Saves an icon with one BMP entry: 24-bit when every pixel is opaque, 32-bit with alpha otherwise. The AND mask marks fully transparent pixels. A grab point other than 0,0 saves a cursor with that hotspot instead. Images up to 256×256 can be saved.
The package includes its `Devs/DataTypes/ICO` descriptor, which matches the `00 00 ?? 00` header on files named `#?.ico` or `#?.cur`, at priority -10, because the header alone is too weak to identify a file.
`formats/ico/ICO.dtyp` is the compiled form of `ICO.dtd`; regenerate it with AROS's `createdtdesc -o formats/ico/ICO.dtyp formats/ico/ICO.dtd` if the recognition rules change.

## Build and test

```sh
make test
make build TARGET=x86_64-aros PACKAGE=targa
```

Each module is written to `dist/<target>/<package>.datatype` and installs at `Classes/DataTypes/<package>.datatype`.

## Versions

Each datatype has its own two-number module and package version. Release tags identify one format, for example `targa-1.0`; changes to another format do not change Targa's version.
