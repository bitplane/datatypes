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

Reads Farbfeld images: 16-bit big-endian RGBA with straight alpha. Channels are rounded to the nearest 8-bit value, and an image whose alpha is zero everywhere loads opaque. Bytes after the last pixel are ignored. Saves Farbfeld with each 8-bit channel widened exactly (times 257), so a saved image loads back unchanged. The package includes its `Devs/DataTypes/FARBFELD` descriptor, which recognises files by their `farbfeld` magic whatever their name.
`formats/farbfeld/FARBFELD.dtyp` is the compiled form of `FARBFELD.dtd`; regenerate it with AROS's `createdtdesc -o formats/farbfeld/FARBFELD.dtyp formats/farbfeld/FARBFELD.dtd` if the recognition rules change.

## TIM

Reads PlayStation TIM textures: 4-bit and 8-bit indexed, 16-bit 5:5:5 and 24-bit RGB. Indexed images use the first palette of their CLUT; entries missing from a short CLUT are black, and images without a CLUT (whose palette lives elsewhere in VRAM) load as grayscale. A CLUT in a 16-bit or 24-bit file is skipped. Images load opaque: the STP bit and transparent black are rendering modes of the PlayStation GPU, not alpha stored in the file. Several TIMs stored back to back in one file are separate pictures, selected with `PDTA_WhichPicture`. Mixed-mode (frame buffer) TIMs are not supported. Saves 24-bit TIM, compositing transparency over white. The package includes its `Devs/DataTypes/TIM` descriptor, which matches the `10 00 00 00` ID and the zero reserved flag bytes on files named `.tim`.
`formats/tim/TIM.dtyp` is the compiled form of `TIM.dtd`; regenerate it with AROS's `createdtdesc -o formats/tim/TIM.dtyp formats/tim/TIM.dtd` if the recognition rules change.

## Build and test

```sh
make test
make build TARGET=x86_64-aros PACKAGE=targa
```

Each module is written to `dist/<target>/<package>.datatype` and installs at `Classes/DataTypes/<package>.datatype`.

## Versions

Each datatype has its own two-number module and package version. Release tags identify one format, for example `targa-1.0`; changes to another format do not change Targa's version.
