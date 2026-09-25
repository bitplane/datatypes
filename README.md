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
The package includes its `Devs/DataTypes/PAM` descriptor. It matches `.pam`, `.pfm` and `.phm` names beginning with `P`, at priority -1, so AROS's PNM descriptors (priority 0) still take `P1`–`P6`. This also recognises `PF4`; other formats beginning with `P` are left to their own descriptors. `formats/pam/PAM.dtyp` is the compiled form of `PAM.dtd`; regenerate it with AROS's `createdtdesc -o formats/pam/PAM.dtyp formats/pam/PAM.dtd` if the recognition rules change.

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
## Palm bitmap

Reads Palm OS bitmaps, versions 0 to 3: 1, 2, 4 and 8-bit indexed, and 16-bit RGB565 direct colour, uncompressed or with scanline, RLE or PackBits compression. Indexed bitmaps use their colour table when they have one; otherwise 1, 2 and 4-bit bitmaps are gray from white to black and 8-bit ones use the Palm system palette. ImageMagick flags its 1, 2 and 4-bit bitmaps as having a colour table without writing one, so at those depths the flag counts only when a table of at most 2^depth entries is actually there. The transparent index or colour becomes transparent when the header flags it, even if it covers the whole image. A file holding a bitmap family (several depths or densities chained together, with or without the high-density separator) is a multi-image picture: `PDTA_WhichPicture` picks a bitmap in file order, `PDTA_GetNumPictures` reports how many there are, and by default the largest, then deepest, bitmap loads. Little-endian (`indexedLE`, `rgb565LE`) version 3 bitmaps and direct colour other than 5:6:5 are rejected.
Saves an uncompressed 8-bit bitmap with a colour table when the image has at most 256 colours, which is lossless, and a 16-bit RGB565 bitmap otherwise. Fully transparent pixels become the transparent colour; partly transparent ones are composited over white.
The package includes its `Devs/DataTypes/PALM` descriptor. Palm bitmaps have no magic number, so it matches files named `#?.palm` of at least 16 bytes, at priority -10. `formats/palm/PALM.dtyp` is the compiled form of `PALM.dtd`; regenerate it with AROS's `createdtdesc -o formats/palm/PALM.dtyp formats/palm/PALM.dtd` if the recognition rules change.

## Palm ImageViewer

Reads the image record of Palm ImageViewer databases (`.pdb` files of type `vIMG`, creator `View`): 1-bit black and white, 2-bit (4 grays) and 4-bit (16 grays), uncompressed or run-length encoded. Images load as 1, 2 or 4-plane pictures with the file's gray levels. The image record ends where the note record starts, so compressed data can't run into the note, and the note itself is ignored, as is a note offset that points to garbage. Record attributes and unique IDs aren't checked, so files from FireViewer, which numbers its records differently, load too. Rows are padded to a byte. ImageViewer needs widths that are a multiple of 16, so every writer produces those; for other widths ImageMagick and netpbm let rows overlap by the partial byte. Doesn't read FireViewer's colour images (type 131) or its large images split into tiles across several records, which have a zero width in the first record's usual place, nor compression values 2 to 7 or other image types.
Saves uncompressed images at the smallest depth that holds every gray exactly, and 16 grays otherwise, rounding to the nearest. Pixels are composited over white and converted to gray by luminance. The width is padded to a multiple of 16 with white, as ImageViewer needs, and the database is named after the picture's object name.
The package includes its `Devs/DataTypes/PDB` descriptor, which matches `vIMGView` at offset 60 and a `#?.pdb` name. `formats/pdb/PDB.dtyp` is the compiled form of `PDB.dtd`; regenerate it with AROS's `createdtdesc -o formats/pdb/PDB.dtyp formats/pdb/PDB.dtd` if the recognition rules change.

## OTB

Reads Nokia OTA bitmaps (`.otb`), the 1-bit format of operator logos and picture messages, with set bits in black. Handles 8-bit and 16-bit sizes and skips extension fields. It ignores the external palette flag and any bytes after the image. Animated bitmaps hold up to 16 pictures: `PDTA_WhichPicture` selects one, the first by default, and `PDTA_GetNumPictures` reports how many there are. The specification packs rows without padding, but ImageMagick pads each row to a byte. The class reads a file as padded when it is long enough to be, and as packed otherwise. Widths that are a multiple of 8, including the usual 72, are the same either way. Saves one-picture OTB with rows padded to a byte, as ImageMagick reads it, and 8-bit sizes when both fit. Pixels are composited over white, then set black if their luminance is below half. Doesn't read compressed bitmaps (the specification never defined the scheme), more than one colour plane, or OTA bitmaps stored as hex text. The package includes its `Devs/DataTypes/OTB` descriptor. OTB has no magic number, so the descriptor requires a `.otb` name and has priority -10.
`formats/otb/OTB.dtyp` is the compiled form of `OTB.dtd`; regenerate it with AROS's `createdtdesc -o formats/otb/OTB.dtyp formats/otb/OTB.dtd` if the recognition rules change.

## MSP

Reads Microsoft Paint images from Windows 1 (`DanM`, uncompressed) and Windows 2 (`LinS`, run-length encoded by row). Images load as one-plane pictures, black and white. The header checksum isn't checked. In version 2 files, a row whose packed size is zero, or whose runs stop short, is white to its end, and a run past the end of its row is cut off. Saves version 1 files: pixels are composited over white, then set white if their luminance is at least half. The package includes its `Devs/DataTypes/MSP` descriptor. The two versions' keys share only their third byte, `n`, so the descriptor matches that byte and requires a `.msp` name.
`formats/msp/MSP.dtyp` is the compiled form of `MSP.dtd`; regenerate it with AROS's `createdtdesc -o formats/msp/MSP.dtyp formats/msp/MSP.dtd` if the recognition rules change.

## Spectrum 512

Reads Atari ST Spectrum 512 pictures, uncompressed SPU and compressed SPC, as 320×200 images with a 48-colour palette on every line. Line 0 has no palette, so it is black, as in netpbm. Palettes use 3 bits per gun, scaled as netpbm scales them. If any palette word has a fourth bit set, the whole picture is read as STE, with 4 bits per gun; netpbm always ignores that bit. The top 4 bits of palette words are ignored. In SPC files the palette length field isn't checked, colour 15 of each palette is black, and a run past the end of the bitmap is cut off. Enhanced SPU files, which start with `5BIT` and store more bits per gun, are rejected, as are SPS and SPX files.
Saves SPU when the picture is 320×200, its top line is black and every colour, after compositing over white, is an ST or STE level. Each line's colours must also fit the 48 palette slots, which the display switches at fixed columns. The writer finds a slot for each colour with a bounded search per line, so an unusually dense picture can fail to save even though a fitting palette exists.
The package includes its `Devs/DataTypes/SPECTRUM` descriptor. SPU has no magic number and a package installs a single descriptor, so it matches any file named `.spu` or `.spc` at priority -10, and the class rejects files that aren't Spectrum 512.
`formats/spectrum/SPECTRUM.dtyp` is the compiled form of `SPECTRUM.dtd`; regenerate it with AROS's `createdtdesc -o formats/spectrum/SPECTRUM.dtyp formats/spectrum/SPECTRUM.dtd` if the recognition rules change.

## NEOchrome

Reads Atari ST NEOchrome `.neo` pictures in all three screen modes: low resolution (320×200, 16 colours), medium (640×200, 4 colours) and high (640×400, black on white). ST palettes use 3 bits per gun, scaled as netpbm scales them. A palette counts as STE, with 4 bits per gun, when one of the colours the mode uses has a fourth bit set; colours the mode doesn't use often hold junk, so they are ignored. Colour-cycling data and bytes after the image are ignored. Files whose flag word isn't zero or whose resolution isn't 0–2 are rejected. Saves NEOchrome when the picture is one of the three screen sizes and, after compositing over white, fits that mode's palette exactly with ST or STE levels. Other pictures can't be saved, because the format can't hold them without loss. The package includes its `Devs/DataTypes/NEO` descriptor. NEOchrome has no magic number, so the descriptor matches the zero flag word and the high byte of the resolution, requires a `.neo` name, and has priority -10.
`formats/neo/NEO.dtyp` is the compiled form of `NEO.dtd`; regenerate it with AROS's `createdtdesc -o formats/neo/NEO.dtyp formats/neo/NEO.dtd` if the recognition rules change.

## XV thumbnail

Reads XV thumbnails, the `P7 332` files XV, GIMP 1.x and makexvpics keep in `.xvpics` directories: 8-bit 3:3:2 RGB, expanded as `v * 255 / max` rounded down, as Pillow and netpbm do. Comment lines are skipped with or without `#END_OF_COMMENTS`, and the size line's maxval may be left out, but when present it must be 255. Saves XV thumbnails at the image's own size: pixels are composited over white, then each channel takes the nearest of the eight (or four, for blue) levels, as netpbm's `pamtoxvmini` does. There is no dithering, and the image is not scaled down to XV's 80×60. The package includes its `Devs/DataTypes/XVTHUMB` descriptor, which recognises files by their `P7 332` magic whatever their name.
`formats/xvthumb/XVTHUMB.dtyp` is the compiled form of `XVTHUMB.dtd`; regenerate it with AROS's `createdtdesc -o formats/xvthumb/XVTHUMB.dtyp formats/xvthumb/XVTHUMB.dtd` if the recognition rules change.

## TIM

Reads PlayStation TIM textures: 4-bit and 8-bit indexed, 16-bit 5:5:5 and 24-bit RGB. Indexed images use the first palette of their CLUT; entries missing from a short CLUT are black, and images without a CLUT (whose palette lives elsewhere in VRAM) load as grayscale. A CLUT in a 16-bit or 24-bit file is skipped. Images load opaque: the STP bit and transparent black are rendering modes of the PlayStation GPU, not alpha stored in the file. Several TIMs stored back to back in one file are separate pictures, selected with `PDTA_WhichPicture`. Mixed-mode (frame buffer) TIMs are not supported. Saves 24-bit TIM, compositing transparency over white. The package includes its `Devs/DataTypes/TIM` descriptor, which matches the `10 00 00 00` ID and the zero reserved flag bytes on files named `.tim`.
`formats/tim/TIM.dtyp` is the compiled form of `TIM.dtd`; regenerate it with AROS's `createdtdesc -o formats/tim/TIM.dtyp formats/tim/TIM.dtd` if the recognition rules change.

## TIM2

Reads PlayStation 2 TIM2 textures: 4-bit and 8-bit indexed with 16, 24 or 32-bit CLUTs, and 16-bit 5:5:5:1, 24-bit and 32-bit direct colour. Alpha is kept as the GS reads it: 0x80 is opaque in 32-bit pixels and CLUT entries, and bit 15 is alpha in 16-bit ones. CLUTs in CSM1 order (all 256-colour CSM1 CLUTs, and 16-colour ones with the compound flag) are put back in index order. Indexed pictures use the first palette of their CLUT; a CLUT shorter than the index range leaves the rest black, and an indexed picture without one loads as grayscale. Every mipmap level of every picture is a separate image, in file order, selected with `PDTA_WhichPicture`; the default is the first picture at full size. Both 16 and 128-byte alignment, user data and extended headers are handled. The total-size field is ignored, since real files get it wrong. CLUT-only (`CLT2`) files are not supported. Textures whose pixels are stored in the GS's swizzled memory order are not marked as such in the file, so they load scrambled. Saves 24-bit TIM2 for opaque pictures and 32-bit otherwise, with alpha halved into the GS's 0–0x80 range, so translucent alpha loses its lowest bit. The package includes its `Devs/DataTypes/TIM2` descriptor, which matches the `TIM2` magic on files named `.tm2` or `.tim2`.
`formats/tim2/TIM2.dtyp` is the compiled form of `TIM2.dtd`; regenerate it with AROS's `createdtdesc -o formats/tim2/TIM2.dtyp formats/tim2/TIM2.dtd` if the recognition rules change.

## MTV and QRT

Reads the output of two ray tracers: MTV (a text line `width height`, then 8-bit RGB triples) and QRT (16-bit little-endian width and height, then per row a row number and the red, green and blue planes). The class tells them apart by content. MTV header lines are read as ImageMagick and netpbm read them: blanks, a `+` sign and anything after the two numbers are allowed, but both numbers must be on the first line. QRT row numbers are ignored, as netpbm ignores them. Images load opaque. An MTV file can hold several images one after another, as ImageMagick writes them. `PDTA_WhichPicture` picks one, and `PDTA_GetNumPictures` reports how many there are. Bytes after the last image that don't form a header line are ignored. Saves one MTV image, compositing transparency over white.
The package includes its `Devs/DataTypes/MTV` descriptor, which covers both formats. Neither has a magic number, so it matches the file name (`#?.mtv`, `#?.pic`, `#?.qrt` or `#?.dis`) at priority -10, and the decoder rejects files that are neither. QRT's own `.raw` name is too generic to claim. `formats/mtv/MTV.dtyp` is the compiled form of `MTV.dtd`; regenerate it with AROS's `createdtdesc -o formats/mtv/MTV.dtyp formats/mtv/MTV.dtd` if the recognition rules change.

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
- **CALS:** the header's first record may be any of the MIL-PRF-28002 records, in any case. `rpelcnt` gives the stored size, and `rorient` the orientation as the spec defines it: the pel path counter-clockwise from rightwards, then the line progression counter-clockwise from the pel path, so `090,270` drawings, which are stored on their side, show upright. An orientation the spec doesn't allow is ignored. Pixels follow T.6, where each line starts white. ImageMagick reads and writes CALS inverted, so a file it wrote opens here as a negative, and real CALS files open the right way round here but negated in ImageMagick.
- **Size:** up to 64M pixels, four times the usual limit, so E-size drawings at 200 dpi load. The decoder keeps one bit per pixel, but picture.datatype keeps a byte per pixel of its own, so a 9400 × 6600 drawing needs about 62MB free.
- **Not supported:** Group 3 two-dimensional (MR) coding, raw Group 4 files (they don't record their width), CALS type 2 (tiled) rasters, and the Digifax header some fax software adds.

Saves raw Group 3 MH at the picture's own width, bits first-to-last, with an EOL before each line and RTC at the end. Pixels are composited over white, then set black if their luminance is under half. Fax machines expect 1728-pixel lines, so pad the picture to that width first if the file is to be sent.

The package includes its `Devs/DataTypes/FAX` descriptor. Raw G3 has no magic number and CALS starts with text, so the one descriptor matches files named `.g3`, `.fax`, `.cal`, `.cals`, `.ct1`, `.c4`, `.mil` or `.ras`, at priority -10. Sun Raster files named `.ras` are still claimed first by the Sun Raster descriptor, which matches their magic at priority 0.
`formats/fax/FAX.dtyp` is the compiled form of `FAX.dtd`; regenerate it with AROS's `createdtdesc -o formats/fax/FAX.dtyp formats/fax/FAX.dtd` if the recognition rules change.

## Sun icon

Reads SunView and OpenWindows icon and cursor files: a `/* Format_version=1, Width=64, Height=64, Depth=1, Valid_bits_per_item=16 */` comment followed by hex items.

- **Depth=1** loads as a one-plane picture, set bits black on white, most significant bit first.
- **Depth=8** loads as 256 grey levels, the stored value being the level, as netpbm shows it. These icons index a palette that the file doesn't carry.
- Items may be 8, 16 or 32 bits wide, and each row is padded to whole items, as SunView's `mpr_static` lays it out.
- The header comment may come after other comments or text (SCCS and RCS ids), its fields may be in any order, and missing fields take XView's defaults: 64 by 64, Depth=1, 16-bit items.
- Items may be separated by commas, white space or comments.
- **Not supported:** depths other than 1 and 8, and other `Format_version`s. Netpbm and XView reject these too.

Saves Depth=1 with 16-bit items, as Sun's `iconedit` and netpbm write it. Pixels are composited over white, then set black if their luminance is below half. XView only loads widths that are multiples of 16, and netpbm only loads rows with an even number of bytes, so the saved width is rounded up to a multiple of 16 with white columns on the right.

The package includes its `Devs/DataTypes/SUNICON` descriptor, which matches files starting with `/* Format_version=1`, whatever their name. Files with another comment before the header still load, but the descriptor doesn't recognise them. That's 5 of the 348 icons in the OpenLook CD-ROM archive.
`formats/sunicon/SUNICON.dtyp` is the compiled form of `SUNICON.dtd`; regenerate it with AROS's `createdtdesc -o formats/sunicon/SUNICON.dtyp formats/sunicon/SUNICON.dtd` if the recognition rules change.

## Utah RLE

Reads Utah Raster Toolkit RLE images with 8-bit pixels: gray, RGB and pseudocolour, each with or without alpha. Pseudocolour is one channel looked up in three colour maps, and RGB through three maps loads too. Maps give the high byte of each 16-bit entry, as the toolkit does, and values past the end of a short map pass through unchanged. Alpha is never mapped. The loader ignores the image's position and shows just the image. Pixels the file doesn't write take the background colour when the header asks for a clear to it, and are black otherwise. In images with alpha they are transparent. A declared alpha channel stays even when it is zero everywhere. Runs past the right edge are clipped and scanlines above the top are dropped. A file can hold several concatenated images: `PDTA_WhichPicture` picks one in file order, the first by default, and `PDTA_GetNumPictures` returns the count.
Pixels other than 8 bits, channel counts other than 1 or 3, and map counts other than 0 or 3 are rejected. Neither ImageMagick nor netpbm reads these correctly.
Saves one image with no background: gray when every pixel is gray and opaque, RGB when opaque, and RGB with alpha otherwise. The package includes its `Devs/DataTypes/UTAHRLE` descriptor, which matches the `52 CC` magic and 8-bit pixels whatever the file's name.
`formats/utahrle/UTAHRLE.dtyp` is the compiled form of `UTAHRLE.dtd`; regenerate it with AROS's `createdtdesc -o formats/utahrle/UTAHRLE.dtyp formats/utahrle/UTAHRLE.dtd` if the recognition rules change.

## ZX Spectrum screen

Reads ZX Spectrum screens and the extended screen formats built on them. The file size tells most of them apart; SXG and MultiArtist files have magic numbers.

| Format | Size in bytes | Picture |
|---|---|---|
| `SCREEN$` (`.scr`) | 6912, or 6913 with a border colour, which isn't shown | 256×192, attributes per 8×8 cell |
| Bitmap only (`.scr`) | 6144 | 256×192, black ink on white paper, as after `CLS` |
| ULAplus (`.scr`) | 6976 | 256×192 with a 64-colour palette |
| Timex hi-colour (`.scr`) | 12288 | 256×192, attributes per 8×1 span |
| Timex hi-colour with ULAplus palette (`.scr`) | 12352 | as above, 64 colours |
| Timex hi-res (`.scr`) | 12289 | 512×192 in two colours, rows doubled to 512×384 |
| Multicolour 8×1 (`.mc` linear bitmap, `.mlt`) | 12288 | 256×192 |
| IFL multicolour 8×2 (`.ifl`) | 9216 | 256×192 |
| Border screen (`.bsc`), with 8×4 multicolour (`.bmc4`) | 11136, 11904 | 384×304 including the border |
| Gigascreen (`.img`) | 13824 | 256×192, the average of two screens |
| Hi-res Gigascreen (`.hrg`) | 24578 | 512×384, the average of two hi-res screens |
| MultiArtist Gigascreen (`.mg1`, `.mg2`, `.mg4`, `.mg8`) | `MGH` header | 256×192, the average of two multicolour screens |
| Speccy eXtended Graphics (`.sxg`) | `\x7FSXG` header | any size up to 16M pixels, 16 or 256 colours |

A 12288-byte file is Timex hi-colour unless its name ends in `.mc` or `.mlt`. Colours use ImageMagick's levels, 0xC0 for normal and 0xFF for bright. Flashing cells show their first phase, ink on paper. ULAplus palettes follow the ULAplus specification: 3-bit levels, with blue's missing low bit set to the OR of its two stored bits. Border colours are never bright. Timex hi-res uses the bright ink selected by bits 3–5 of the last byte, and the complementary paper. Files shorter than 6912 bytes (other than 6144) are truncated, and other sizes are rejected. Not supported: packed SXG files, three-screen `.3` and `.rgb` tricolour files, `+3DOS` headers, attribute-only `.atr` files, and the other RECOIL ZX formats (`.bsp`, `.chx`, `.zxp`, `.sev` and so on).
Saves a standard 6912-byte screen when the picture is 256×192 and, after compositing over white, uses only Spectrum colours with at most two per 8×8 cell, both normal or both bright (black goes with either). Otherwise it saves a 12288-byte Timex hi-colour screen if each 8×1 span meets the same rule. Other pictures can't be saved, because neither layout can hold them without loss. In a cell or span with two colours, the one with the lower colour number is paper.
The package includes its `Devs/DataTypes/ZXSCR` descriptor. Most of these formats have no magic, so the descriptor only requires 16 bytes of data and one of the file name patterns above, at priority -11. That is one below the stock GEM IMG descriptor, so GEM `.img` files still go to the stock class. A Gigascreen `.img` whose bytes 0, 2, 4 and 6 are zero matches GEM's mask first and won't open. Windows screen savers also use `.scr`. The descriptor claims them too, but the loader rejects them because of their size.
`formats/zxscr/ZXSCR.dtyp` is the compiled form of `ZXSCR.dtd`; regenerate it with AROS's `createdtdesc -o formats/zxscr/ZXSCR.dtyp formats/zxscr/ZXSCR.dtd` if the recognition rules change.

## CompuServe RLE

Reads CompuServe RLE (VIDTEX) pictures: `ESC G M` for 128x96 and `ESC G H` for 256x192. Each character after the header is a run of its code minus 32, alternating black and white, black first; an empty run just switches colour. As in netpbm's `cistopbm`, anything before the header is skipped, other control characters (CR, LF, BEL, NUL) are ignored, and any ESC, normally the closing `ESC G N`, ends the picture. Real files often stop a pixel or more short of the end before `ESC G N`, so pixels left unset are white, as netpbm shows them. A file without an ESC must fill the picture, or it is truncated. Runs past the last pixel are clamped, so trailing junk after a full picture is ignored. Bytes with the top bit set lose it, as on a 7-bit terminal line; netpbm counts them as long runs instead, which makes no difference in any collected file. The 640x200 `ESC G S` mode that some later terminals added isn't read, as netpbm rejects it too. Saves 128x96 when the picture fits, as `pbmtocis` does, and 256x192 otherwise, cropping larger pictures and padding smaller ones with white. Pixels are composited over white, then set black if their luminance is below half. Runs are at most 94 long, joined by empty runs, avoiding DEL. The package includes its `Devs/DataTypes/CIS` descriptor. It matches `ESC G` together with a name ending in `.rle` or `.cis`, at priority 0: two bytes are too weak to match on alone, and every collected file is named `.rle`.
`formats/cis/CIS.dtyp` is the compiled form of `CIS.dtd`; regenerate it with AROS's `createdtdesc -o formats/cis/CIS.dtyp formats/cis/CIS.dtd` if the recognition rules change.

## DDS

Reads DirectDraw Surface textures. Block-compressed images can be DXT1, DXT3 or DXT5 (BC1 to BC3), ATI1/BC4U (BC4, shown gray), or ATI2, BC5U and BC5S (BC5: red and green, with blue 0; signed values show as v + 128 with blue 128). DX10 headers add BC1 to BC5, BC6H and BC7. Uncompressed images can use any 8 to 32-bit RGB, luminance or alpha-only layout described by bit masks, 8-bit palettes, or the DX10 formats R8G8B8A8, B8G8R8A8, B8G8R8X8 and R10G10B10A2. BC6H is HDR: values are clamped to 0-1 and encoded with the sRGB curve. DXT1 blocks can be transparent. Alpha in other uncompressed formats and palettes counts only when the header declares it. DX10 premultiplied alpha is converted to straight alpha, and DX10's opaque alpha mode is honoured. Declared alpha is preserved even when it is zero everywhere.

Mip levels, cube faces, array slices and volume slices are separate pictures, counted in file order: each face or slice in turn, then its mip levels from largest to smallest. Without `PDTA_WhichPicture` the first one loads. Levels missing from the end of a file aren't counted. A file whose first picture is cut short fails to load. Each picture can have at most 16M pixels.

Not supported: DXT2 and DXT4 (premultiplied DXT3 and DXT5), BC4 signed, RXGB, YUV formats, the float and 16-bit-per-channel D3D formats, and DX10 formats other than those above, including the `_SRGB` codes of BC1 to BC3 and B8G8R8A8. Neither ImageMagick nor Pillow reads these.

Saves uncompressed DDS with one image and no mip levels: 24-bit RGB when every pixel is opaque, 32-bit ARGB otherwise.
The package includes its `Devs/DataTypes/DDS` descriptor, which matches the `DDS ` magic and the 124-byte header size on files named `#?.dds`. `formats/dds/DDS.dtyp` is the compiled form of `DDS.dtd`; regenerate it with AROS's `createdtdesc -o formats/dds/DDS.dtyp formats/dds/DDS.dtd` if the recognition rules change.

## FTEX

Reads FTEX textures from Independence War 2 (`.ftc` compressed and `.ftu` uncompressed files). Formats can be DXT1 (BC1), whose three-colour blocks can be transparent, or 24-bit RGB. A file can hold several formats, each with its own chain of mip levels. They are separate pictures, counted in file order: each format in the directory in turn, then its mip levels from largest to smallest. Without `PDTA_WhichPicture` the first one loads, as in Pillow. Levels missing from the end of a file aren't counted, and a file whose first picture is cut short fails to load. A level may hold more bytes than its pixels need. A declared level count of 0 still reads the top level. Directory entries with other format numbers are skipped. Each picture can have at most 16M pixels.

Only Pillow reads FTEX. ImageMagick and netpbm don't. The decoder matches Pillow pixel for pixel. Pillow also reads only the first picture, and it rejects files with more than one format.

Saves an uncompressed (`.ftu`) file with one 24-bit RGB format and one mip level, compositing transparency over white.
The package includes its `Devs/DataTypes/FTEX` descriptor, which matches the `FTEX` magic on files named `#?.ftc` or `#?.ftu`. `formats/ftex/FTEX.dtyp` is the compiled form of `FTEX.dtd`; regenerate it with AROS's `createdtdesc -o formats/ftex/FTEX.dtyp formats/ftex/FTEX.dtd` if the recognition rules change.

## Pixar

Reads 8-bit Pixar Image Computer (picio) pictures: the `.pxr` files Photoshop writes, and the `.pic` files from Pixar's own software and from tools like Altamira Composer. RGB and RGBA load as they are. A single channel loads as grey, and red plus alpha loads as grey with alpha. Pixels can be dumped raw or encoded as run-length packets split into disk blocks, in one tile or many. Edge tiles are stored full size, and the loader drops the part outside the picture. A null tile shows as black, or as transparent when the picture has alpha. Matted-to-black alpha is premultiplied, so the loader converts it to straight alpha. Unassociated alpha loads unchanged, even when it is zero everywhere.

12-bit storage is not supported. Its samples are fixed point, with 1.0 at 2048 and headroom above white, and no 12-bit sample files turned up to test against. Other channel combinations are rejected too.

Saves one dumped 8-bit tile at offset 1024, the layout Photoshop writes. An opaque picture is saved as RGB, which Pillow can read. Anything with transparency is saved as RGBA with unassociated alpha.

The package includes its `Devs/DataTypes/PIXAR` descriptor. It matches the magic `80 E8 00 00` in files named `.pxr`, `.pic`, `.picio` or `.pixar`.
`formats/pixar/PIXAR.dtyp` is the compiled form of `PIXAR.dtd`; regenerate it with AROS's `createdtdesc -o formats/pixar/PIXAR.dtyp formats/pixar/PIXAR.dtd` if the recognition rules change.

## SIXEL

Reads DEC SIXEL images, the terminal graphics that libsixel, ImageMagick, netpbm's `ppmtosixel` and gnuplot write. An image is a device control string that opens with `ESC P` or the 8-bit `0x90`, then parameters and `q`, and closes with `ESC \` or `0x9C`. Text and escape sequences before it, as in terminal captures, are skipped. Colours can be RGB or HLS (DEC's hues, so 0 is blue), and there are 1024 registers. Unset registers start as the VT340's 16 colours, then xterm's 6×6×6 cube and grey ramp, as in ImageMagick. A register holds its last definition, so redefining one recolours pixels already drawn, as on a VT340. The raster attributes give the smallest size, and drawing past them makes the image bigger. When P2 is 1, pixels that are never drawn are transparent. Otherwise they take register 0. The pixel aspect ratio (P1, and Pan and Pad) is ignored, as ImageMagick and most terminals ignore it. Line breaks can fall anywhere, even inside a number. A file can hold several images, such as the frames of an animation. `PDTA_WhichPicture` picks one, the first by default, and `PDTA_GetNumPictures` reports how many there are. A file that ends before the string terminator is rejected as truncated.
Saves one image with RGB registers, in percent, the only precision SIXEL has. Up to 256 colours are kept exactly, and images with more are reduced to 256 by median cut, without dithering. Pixels with alpha below 128 are left undrawn, with P2 set to 1, and the rest are composited over white. Such images define register 0 as white, so readers that ignore P2 show white there.
The package includes its `Devs/DataTypes/SIXEL` descriptor. It matches an ASCII file that starts with ESC and is named `#?.six` or `#?.sixel`, so terminal captures that begin with another escape sequence also load. Files using the 8-bit `0x90` introducer count as binary to AROS and aren't recognised, because a package ships one descriptor. The decoder reads them. `formats/sixel/SIXEL.dtyp` is the compiled form of `SIXEL.dtd`; regenerate it with AROS's `createdtdesc -o formats/sixel/SIXEL.dtyp formats/sixel/SIXEL.dtd` if the recognition rules change.

## Xcursor

Reads X11 cursor files, as shipped in cursor themes on Linux desktops. Each image is 32-bit ARGB with premultiplied alpha. It loads as straight alpha, converted the way GIMP converts it, and the alpha channel is always kept. A file holds several images: nominal sizes, and animation frames within each size. `PDTA_WhichPicture` picks one by its position among the image entries in the table of contents, and `PDTA_GetNumPictures` reports how many there are. Otherwise the largest image loads, and the first of several equally large frames. Comments and unknown chunk types are skipped. Hotspots and frame delays are ignored, and a hotspot outside the image, which libXcursor rejects, doesn't stop the file loading. As in libXcursor, the file fails to load if any image entry is damaged or truncated, or if a side is over 32767 pixels.
Saves a one-image cursor with its hotspot at the top left and its larger side as the nominal size. Colours are premultiplied, so semi-transparent pixels lose some precision and fully transparent ones lose their colour. The package includes its `Devs/DataTypes/XCURSOR` descriptor, which matches the `Xcur` magic whatever the file is called, since theme cursors have no extension.
`formats/xcursor/XCURSOR.dtyp` is the compiled form of `XCURSOR.dtd`; regenerate it with AROS's `createdtdesc -o formats/xcursor/XCURSOR.dtyp formats/xcursor/XCURSOR.dtd` if the recognition rules change.

## PAA

Reads Bohemia Interactive PAA and PAC textures from Operation Flashpoint, Arma and DayZ. The block-compressed types are DXT1 to DXT5; DXT1 blocks can be transparent, as Direct3D reads them, and DXT2 and DXT4 are premultiplied, so their alpha is divided out. The others are ARGB4444, ARGB1555, ARGB8888 and 8-bit gray with alpha, all in Direct3D's channel order, with their alpha kept even when it is zero everywhere. Also reads Operation Flashpoint's 8-bit index-palette files, which have no type word, including the 1997 demo's, which have no taggs either; their levels are run-length or LZSS coded, and an index past the end of the palette is black. DXT levels flagged in the width's top bit are LZO-compressed, as Arma 2 and later write them; the other types are LZSS-compressed, and the checksum after the data must match, summed as signed or as unsigned bytes. Some third-party writers store those levels uncompressed, which is accepted when the level is exactly the uncompressed size. Taggs are skipped, including the swizzle tagg that normal and specular maps carry, so those load with their channels as stored. Palettes in typed files and bytes after the end marker are ignored.

Each mip level is a picture, largest first. `PDTA_WhichPicture` selects one, the first by default, and `PDTA_GetNumPictures` reports how many there are. Levels cut short at the end of a file aren't counted, and a file whose first level is cut short fails to load. Each level can have at most 16M pixels.

Saves one ARGB8888 level, LZSS-compressed, with the average colour, maximum colour and offset taggs, and the alpha flag tagg when a pixel isn't opaque. Smaller mip levels are not written. Pictures whose compressed level passes the 24-bit size field, such as 2048×2048 noise, can't be saved.
The package includes its `Devs/DataTypes/PAA` descriptor. The typed and index-palette files start with different bytes, and the demo's with none in particular, so it matches files named `#?.paa` or `#?.pac` at priority -10, and the decoder rejects files that aren't PAA. `formats/paa/PAA.dtyp` is the compiled form of `PAA.dtd`; regenerate it with AROS's `createdtdesc -o formats/paa/PAA.dtyp formats/paa/PAA.dtd` if the recognition rules change.

## Japanese PC pictures

Reads the compressed picture formats of 1990s Japanese computers, the NEC PC-98 and PC-88, Sharp X68000, FM TOWNS and MSX. The class tells them apart by their magic:

- **MAG** (Maki-chan 2, `MAKI02`), 16 or 256 colours, from any machine. The picture is cropped to the header's rectangle, including a left edge that doesn't fall on a flag unit. MSX files also read in their screen modes: screen 5, 7 and 8, screen 6 at 2 bits per pixel, and the YJK modes of screens 10 to 12, with or without the palette colours of screens 10 and 11. Palette values are used as stored, because the MAG specification has writers fill the low bits.
- **MKI** (Maki-chan 1, `MAKI01A` and `MAKI01B`), 640×400 in 16 colours.
- **Pi**, 16 or 256 colours, including files that leave the palette out for the specification's default one.
- **PIC**: X68000 pictures in 16, 256, 32768 and 65536 colours; PC-88VA pictures in 256, 4096 and 65536 colours, including the dithered 256-colour mode stored as pairs; FM TOWNS and Macintosh pictures; and the generic model's 16 and 256-colour packed palettes, 4096, 32768, 65536 and 16M colours. Comments starting `/MM/` mark MSX pictures.

Pi and MKI palettes are stored with the machine's missing low bits as zeros. They're widened to 8 bits the way the machine named in the file shows them: 4 bits for the PC-98 and PC-88, 3 for the MSX, 5 for the X68000 and FM TOWNS, and 5, 6 and 5 for the PC-88VA. Files wrapped in a 128-byte MacBinary header also load.

Pixels aren't always square, so pictures are scaled to the intended aspect by repeating lines or columns. MAG files with the 200-line bit set, PC-8001 MAG files and PC-88 Pi files get double-height lines. PC-88VA PIC pictures are sized against the 640×400 screen outside HR mode, so 200-line pictures get double-height lines and 320-pixel ones double-width columns. Pi files with a 2:1 aspect ratio are scaled the way the ratio says. MSX MAG files follow their screen mode. X68000 pictures in 512×512 modes are shown unscaled: their pixels are wider than tall, but by a ratio the specification gives two ways (16:9 and 13:9), and not a whole number.

Not supported: MKI files of any size but 640×400; Pi pictures 1 or 2 pixels wide, which the specification codes differently; PIC files of other models or colour depths, such as the generic model's undefined 32-bit colour. Truncated files are rejected rather than shown in part.

Saves MAG: 16 colours when the picture has at most 16, 256 when it has at most 256, compositing transparency over white. Pictures with more than 256 colours can't be saved. Saved files have square pixels and machine code 0.

The package includes its `Devs/DataTypes/JAPANPC` descriptor. One descriptor has to cover four formats with different magic numbers, so it matches only the file name: `#?.(mag|max|mki|pi|pic)`, at priority -10, and the decoder rejects files named like that that aren't one of these formats, such as Softimage or PC Paint `.pic` files. A MAG with a long ASCII comment still loads: when AROS finds only the generic ASCII type, it also checks binary descriptors.
`formats/japanpc/JAPANPC.dtyp` is the compiled form of `JAPANPC.dtd`; regenerate it with AROS's `createdtdesc -o formats/japanpc/JAPANPC.dtyp formats/japanpc/JAPANPC.dtd` if the recognition rules change.

## Atari Falcon and TT

Reads the Atari Falcon and TT paint formats:

- **True colour (RGB565):** GodPaint `.god`, IndyPaint `.tru`, EggPaint and Spooky Sprites `.trp`, COKE `.tg1`, Rembrandt `.tcp`, and 384×240 Falcon screen dumps `.ftc`.
- **Prism Paint and TruePaint** `.pnt`/`.tpi`: 1, 2, 4 and 8 bitplanes with a VDI palette, 16-bit and 24-bit, each uncompressed or PackBits-packed.
- **DuneGraph:** `.dg1`, and `.dc1` with any of its four packing methods.
- **Fuckpaint** `.pi4`/`.pi7`/`.pi9`: 320×200, 320×240 and 640×480 with 256 colours.
- **DEGAS files in TT modes:** `.pi4` (320×480, 256 colours), `.pi5` (640×480, 16 colours) and `.pi6` (1280×960, black on white).

The class identifies the format by content, not by name. It checks the ID for the formats that have one. The rest are recognised by exact file size, and for TT DEGAS the resolution word as well.

How colours are scaled:

- RGB565 channels repeat their top bits into the low ones.
- Falcon palette entries have 6 bits per gun, repeated the same way, so `$FC` is full brightness.
- TT palette entries have 4 bits per gun.
- VDI levels (0–1000) are rounded to 8 bits, and values above 1000 are clamped.

Pixels load as stored, without correcting their shape. TT low resolution and ST medium resolution pictures therefore look squeezed, as they do in AROS's DEGAS class.

A Rembrandt file can hold several pictures. `PDTA_WhichPicture` picks one, and `PDTA_GetNumPictures` reports how many there are.

DuneGraph's packer stops once only zeros are left, so `.dc1` data past the stored packed size is zero. A file shorter than its packed size is truncated.

Unsupported:

- Rembrandt's compression flag, which was never implemented.
- ICE-packed EggPaint files.
- The `st.pi5` kind of 320×240 ST picture in a DEGAS file.
- Other Atari 8-bit `.pi9` pictures.

Saves 24-bit uncompressed Prism Paint (`.pnt`), the only lossless RGB variant among these formats, compositing transparency over white.

The package includes its `Devs/DataTypes/FALCON` descriptor. The formats share no magic number, so it matches only the file name (`#?.(god|tru|tg1|tcp|trp|pnt|tpi|dg1|dgu|dc1|pi4|pi5|pi6|pi7|pi9|ftc)`), and the decoder rejects anything else. Its priority is -11, below MacPaint's -10, so MacPaint's mask is checked first on `.pnt` files.
`formats/falcon/FALCON.dtyp` is the compiled form of `FALCON.dtd`; regenerate it with AROS's `createdtdesc -o formats/falcon/FALCON.dtyp formats/falcon/FALCON.dtd` if the recognition rules change.
## C64 picture

Reads Commodore 64 bitmap pictures from paint programs and the demo scene, in the Pepto palette that RECOIL and VICE use. Multicolour pixels are shown two pixels wide, so pictures are 320×200. FLI pictures are 296×200 without the three character columns lost to the FLI bug, as RECOIL shows them. Interlaced pictures show the average of their two frames, the way they looked on a real screen.
- Hires: Art Studio and Interpaint hires (`.art` `.aas` `.iph` `.hpi` `.hpc`), Doodle (`.dd` `.ddp`, packed `.jj`), Hires-Editor and Run Paint (`.het` `.rph` `.rpo`), Hi-Eddi and Image System (`.hed` `.ish`), plain hires bitmaps (`.hbm` `.hir`) and AFLI (`.afl`).
- Multicolour: Koala Painter and its copies (`.koa` `.kla` `.gig` `.ipt` `.rpm` `.fpt`, packed `.gg`), Amica Paint (`.ami`), Advanced Art Studio (`.ocp` `.mpi`), Drazpaint (`.drz` `.drp`, packed or not), Blazing Paddles (`.pi` `.bpl`), Wigmore Artist 64 (`.a64` `.wig`), Rainbow Painter (`.rp`), Dolphin Ed and Vidcom 64 (`.dol` `.vid` `.vic`), Picasso 64 (`.p64`), CDU-Paint (`.cdu`), Cheese (`.che`), Image System (`.ism`), Saracen Paint (`.sar`), Paint Magic (`.pmg`) and uncompressed Micro Illustrator (`.mil`).
- FLI: FLI Graph and FLI Designer (`.fli` `.fd2`), Blackmail FLI with a background per line (`.bml` `.flg`), FLI Editor (`.fed`) and Flimatic (`.flm`).
- Interlaced: Drazlace (`.drl` `.dlp`, packed or not, including its one-pixel shift), True Paint (`.mci`), Fuckpaint (`.fp`), Gunpaint (`.gun` `.ifl`), Funpaint (`.fun` `.fp2`, packed or not), Flash FLI (`.ffli` `.ffl`), Hires Interlace (`.hlf` `.hie`), Hireslace (`.hle`), ECI (`.eci`, packed `.ecp`), Interlace Hires Editor (`.ihe`) and Vertical Hires Interlace (`.vhi`).

The files have no magic, so the loader finds the format from the length, the load address in the first two bytes and any signature (Drazpaint, Drazlace, Funpaint, Flash FLI). The extension breaks ties between formats of the same length, such as 32770-byte ECI and Hireslace, or 8002-byte hires bitmaps and Run Paint. Without a deciding extension, the first format that fits the length and load address wins, then the first that fits the length. Packed formats with no signature (`.gg` `.jj` `.ami` `.ecp`) need their extension. A Koala file at `$6000` or `$4400` with up to 64 bytes of junk at the end, which is common in files copied off disk, loads as Koala. RECOIL runs those through its packed-Koala decoder instead and shows noise. RLE runs past the end of the picture are cut short. A packed stream that ends early, or a named file shorter than its format, is truncated.

Not supported: MUFLI and MUIFLI (sprites under an FLI bitmap, documented only by RECOIL's code), Pixel Perfect (`.pp` is PowerPacker's extension on Amiga-family systems), packed Micro Illustrator, True Paint, Blackmail FLI, Flimatic and Hires Manager, Big FLI, Super Hires, NUFLI, UFLI, SHF and other sprite-based modes, character-set and PETSCII screens, sprite and font files, and GoDot. ImageMagick, Pillow and netpbm read none of these formats; RECOIL was the reference throughout.

Saves a 320×200 picture whose pixels, composited over white, are all Pepto colours. A picture made of pixel pairs with at most three colours per 4×8 cell besides one shared background is saved as Koala Painter. Otherwise, one with at most two colours per 8×8 cell is saved as Art Studio hires. Anything else can't be saved without loss and fails with invalid data.
The package includes its `Devs/DataTypes/C64` descriptor. With no magic to match, it requires two bytes of data and one of the extensions above, at priority -10. To fit `createdtdesc`'s 256-byte line, the pattern leaves out a few rare alternative extensions (`.lre` `.hre` `.cwg` `.fly` `.fgs` `.gih` `.bed` `.mpic` `.gcd` `.mon`), whose formats load under their main extensions. `.art` is also used by Atari ST Art Director.
`formats/c64/C64.dtyp` is the compiled form of `C64.dtd`; regenerate it with AROS's `createdtdesc -o formats/c64/C64.dtyp formats/c64/C64.dtd` if the recognition rules change.

## BLP

Reads Blizzard BLP textures from Warcraft III (BLP1) and World of Warcraft (BLP2). Palettized images have 8-bit indices into a 256-colour BGRA palette, and a separate 1, 4 or 8-bit alpha channel when the header declares one. BLP2 files can also be DXT1, DXT3 or DXT5 compressed, or uncompressed BGRA. Alpha counts only when the header's alpha depth is nonzero, and DXT1 blocks are then transparent where the block says so. Two quirks of Pillow's writer are accepted: it declares alpha in palettized files but keeps it in the palette's fourth byte instead of an alpha channel, and in BLP1 files it records the pixel offset 8 bytes too early.

Mip levels are separate pictures, largest first. Without `PDTA_WhichPicture` the full-size image loads, and `PDTA_GetNumPictures` returns the number of levels present in the file. Each picture can have at most 16M pixels.

Not supported: JPEG-compressed BLP1 and BLP2, which is how most Warcraft III textures are stored (it needs a JPEG decoder that handles Blizzard's 4-channel JPEG), and BLP0, which keeps its mip levels in separate `.b00` to `.b15` files.

Saves BLP2 with one level: palettized when the image has at most 256 distinct colours, uncompressed BGRA otherwise, with an 8-bit alpha channel when any pixel isn't opaque.
The package includes its `Devs/DataTypes/BLP` descriptor, which matches the `BLP` magic on files named `#?.blp`. `formats/blp/BLP.dtyp` is the compiled form of `BLP.dtd`; regenerate it with AROS's `createdtdesc -o formats/blp/BLP.dtyp formats/blp/BLP.dtd` if the recognition rules change.

## Amiga icons

Reads Workbench `.info` icons, in every form an icon keeps its images in:

- **Planar images** (OS 1.x to 3.1), of any depth. The file holds no colours, so pens take Workbench's defaults: the OS 1.3 blue, white, black and orange for revision 0 icons, as netpbm shows them; the OS 2 grey, black, white and blue for later ones; MagicWB's eight colours for images that reach pens 4 to 7; and for deeper images, AROS's default screen palette at the image's depth, which adds red, green, dark blue and yellow as the last four pens and the pointer's reds as pens 17 to 19, with the other pens black. Most 8-plane icons use just those pens; 4- to 6-plane sets that shipped their own palette show their other pens black, as AROS draws them. Pen 0 is opaque, as Workbench draws it. PlanePick and PlaneOnOff are applied, so a selected image with PlanePick 0 is a solid PlaneOnOff pen, as `DrawImage` draws it.
- **NewIcons** held in `IM1=` and `IM2=` tooltypes after the `*** DON'T EDIT THE FOLLOWING LINES!! ***` line, with pen 0 transparent when the header says so.
- **OS 3.5 colour icons** ("GlowIcons"): `IMAG` chunks in the `FORM ICON` after the icon, packed or raw, with a packed or raw palette, or reusing the first image's palette, and a transparent pen when flagged.
- **ARGB images**: zlib `ARGB` chunks written by AROS, MorphOS and OS4 icon tools. The stored packed size is ignored in favour of the zlib stream's own end, because AROS stores the size and the others store it less one.

A file holds up to eight images: normal and selected images of each kind. `PDTA_WhichPicture` picks one by its position in the file, and `PDTA_GetNumPictures` reports how many there are. Otherwise the image Workbench would show loads: ARGB before OS 3.5, then NewIcons, then planar, and the normal image before the selected one. Revision 0 drawers that carry DrawerData2 anyway, as some early Workbench 2 icons do, still find their `FORM ICON`.

**Not supported:**

- **PNG icons** (OS4 and MorphOS `.info` files that are PNG files). The stock PNG class opens them, showing the first image.
- **`png ` chunks inside a `FORM ICON`.** They are skipped.

Tooltypes, positions, drawer windows and the frame and aspect flags are ignored.

Saves a project icon: a two-plane image in the OS 2 pens for old Workbenches, then a `FORM ICON` holding the picture itself. That is a packed OS 3.5 palette image when the picture has at most 256 colours and every pixel is fully opaque or fully transparent, and a zlib `ARGB` image otherwise. The colour of fully transparent pixels isn't kept. OS 3.5 images are at most 256 pixels a side, so larger pictures can't be saved.

The package includes its `Devs/DataTypes/INFO` descriptor, which matches the `0xE310` magic and version 1 on files named `#?.info`, so GNU texinfo files with the same extension aren't claimed.
`formats/info/INFO.dtyp` is the compiled form of `INFO.dtd`; regenerate it with AROS's `createdtdesc -o formats/info/INFO.dtyp formats/info/INFO.dtd` if the recognition rules change.

## AVS and AAI

Reads two headerless 32-bit rasters: Stardent AVS X images (big-endian 32-bit width and height, then alpha, red, green and blue bytes) and Dune HD AAI images (the same with little-endian sizes and blue, green, red, alpha pixels). The class tells them apart by content. Sides are at most 65535, so the two byte orders never both give a valid header. Alpha is straight and always kept, with two exceptions. An AVS image whose alpha is zero everywhere loads opaque, because original AVS files such as the format's `mandrill.x` sample leave it zero. In AAI, 254 is opaque, as Dune's tools and ImageMagick write it. A file can hold several images of one kind back to back, as ImageMagick writes them. `PDTA_WhichPicture` picks one and `PDTA_GetNumPictures` reports how many there are. A zero-sized header or fewer than 8 trailing bytes ends the list. Saves one image with its alpha, as AAI if the picture came from a `.aai` file and as AVS otherwise. A fully transparent picture saved as AVS reloads opaque.
The package includes its `Devs/DataTypes/AVS` descriptor, which covers both formats. Neither has a magic number or a byte both always share, so it matches the file name (`#?.avs`, `#?.x` or `#?.aai`) at priority -10, and the decoder rejects files that are neither. `formats/avs/AVS.dtyp` is the compiled form of `AVS.dtd`; regenerate it with AROS's `createdtdesc -o formats/avs/AVS.dtyp formats/avs/AVS.dtd` if the recognition rules change.

## Alias/Wavefront RLA and PIX

Reads two run-length encoded formats from 3D renderers and tells them apart by content.
Wavefront RLA files have gray or RGB colour channels of 1 to 16 bits. Deeper channels are rounded to 8 bits, and old files that leave the bit count at 0 load as 8-bit. The first matte channel becomes alpha. Colour is stored multiplied by the matte, as renderers write it and OpenImageIO reads it, so it is converted to straight alpha. ImageMagick shows the stored values unconverted. Further matte channels, auxiliary channels (Z buffer and 3ds Max G-buffer data) and the image's position in its full window are ignored. Revisions `0xFFFE`, `0xFFFD` and 0 load. An RLA file can chain several images through its headers. `PDTA_WhichPicture` picks one in file order, the first by default, and `PDTA_GetNumPictures` returns the count. Float and 32-bit channels are rejected, as are the older RLB layout and 3ds Max RPF files.
Alias PIX files hold 24-bit colour or an 8-bit gray matte, which loads as a gray image. Runs may carry on into the next row, as ImageMagick reads them. The header's offset fields are ignored.
Saves an 8-bit RGB RLA file. When the picture has transparency it adds a matte channel and multiplies the colour by it, so colour under partial or zero alpha loses precision. PIX isn't saved because it can't hold alpha. The package includes its `Devs/DataTypes/Alias` descriptor. PIX has no magic number, so the descriptor matches the file name (`#?.rla`, `#?.pix`, `#?.als` or `#?.alias`) at priority -10, and the decoder rejects files that are neither format. Packages ship one descriptor, which rules out a separate content match on the RLA revision field.
`formats/alias/ALIAS.dtyp` is the compiled form of `ALIAS.dtd`; regenerate it with AROS's `createdtdesc -o formats/alias/ALIAS.dtyp formats/alias/ALIAS.dtd` if the recognition rules change.

## Atari ST compressed paint

Reads the compressed pictures of eight Atari ST paint programs:

- Tiny (`.tny`, `.tn1`–`.tn6`), including the colour-cycling modes 3–5, whose cycling data is ignored
- CrackArt (`.ca1`–`.ca3`), compressed or not
- Imagic (`.ic1`–`.ic3`)
- STAD (`.pac`), packed across or down
- compressed Dali (`.lpk`, `.mpk`, `.hpk`)
- Pablo Paint (`.ppp`, `.pa3`)
- Picworks (`.cp3`)
- PaintShop (`.psc`), which alone keeps its own size of up to 640×400

The others give a 320×200 16-colour, 640×200 4-colour or 640×400 black-on-white screen, as `neo` does. The ST and STE palette rules are also `neo`'s. Imagic's film deltas load with zeros where the base picture would show.

Some writers stop a STAD file one byte short of the screen. That last byte is left white rather than rejecting the file. Tiny files with more than 512 bytes after their data are rejected. Programs on other systems also use the `.tn4` name, and RECOIL shows those files as stripes.

Unsupported:

- Pablo's compressed variant (type 29), which is undocumented, with no known samples
- uncompressed Imagic pictures, of which none are known

Uncompressed Dali (`.sd0`–`.sd2`) and Paintworks belong to the ST screen class.

Saves Tiny for the three screen sizes, with the same colour rules as NEOchrome. The package includes two descriptors. `STPAINT` matches the other extensions by name at priority -10, while `STPAINT_PAC` matches `.pac` files beginning with `pM8` at priority -9. This lets Bohemia PAA `.pac` files reach the PAA descriptor. The decoder recognises Imagic (`IMDC`), STAD (`pM85`/`pM86`), PaintShop (`tm89`), Pablo and CrackArt by their signatures, and the rest by extension. Dali stores its resolution only in the extension.
`formats/stpaint/STPAINT.dtyp` and `STPAINT_PAC.dtyp` are compiled from their matching `.dtd` files with AROS's `createdtdesc`.

## Build and test

```sh
make test
make build TARGET=x86_64-aros PACKAGE=targa
```

Each module is written to `dist/<target>/<package>.datatype` and installs at `Classes/DataTypes/<package>.datatype`.

## Versions

Each datatype has its own two-number module and package version. Release tags identify one format, for example `targa-1.0`; changes to another format do not change Targa's version.
