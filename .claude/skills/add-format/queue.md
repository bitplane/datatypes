# Format queue

Take the first entry that is not done, claimed or blocked. See `SKILL.md`, section 1, for how to tell whether an entry is claimed.

Done: Targa, PCX, QOI, WBMP, SGI, Sun Raster, XBM. Branches and PRs show what's in progress.

Stock AROS classes (don't add these): BMP, GIF, ILBM, JPEG, PNG, PNM (P1–P6), TIFF, WebP, HEIC, JPEG XL, Degas, GEM IMG.

| # | Format | Name | Notes |
|---|---|---|---|
| 1 | XPM | `xpm` | X11 pixmaps, a text format. Supports XPM3 and XPM2. Colour names need an X11 colour table of our own. |
| 2 | Farbfeld | `farbfeld` | 16-bit RGBA with a magic number. |
| 3 | PAM and PFM | `pam` | Netpbm P7 plus PFM (`PF`, `Pf`, and Pillow's `PF4`). A new basename next to the stock `pnm`. PFM is float data. |
| 4 | XWD | `xwd` | X window dumps. Big-endian header, visual classes, colour maps. |
| 5 | MacPaint | `macpaint` | PackBits, 576×720, no magic. May have a MacBinary header. |
| 6 | ICO and CUR | `ico` | Multi-image: largest and deepest by default. Entries may be BMP or PNG. **Blocked** on how to decode PNG entries: `png.library` (in every SDK as `png.h` and `libpng_rel.a`, not yet compile-checked) or the zlib wrapper. |
| 7 | DCX | `dcx` | Multi-page PCX. May reuse the PCX codec through `common/` once there are two users. |
| 8 | DDS | `dds` | Uncompressed, BC1–BC5, then BC6H and BC7. Mipmaps and cube faces are multi-image. |
| 9 | PSD and PSB | `psd` | The flattened composite image only. RLE; CMYK, Lab and 16-bit need converting. Read-only. |
| 10 | Radiance HDR | `hdr` | RGBE with RLE scanlines. Float to 8-bit per `SKILL.md`. |
| 11 | FITS | `fits` | Astronomy data: BITPIX, BSCALE/BZERO, several HDUs. |
| 12 | ICNS | `icns` | Apple icons: packed RLE entries plus PNG and JPEG 2000. **Blocked** like ICO on decoding PNG entries; JPEG 2000 entries are unsupported. |
| 13 | Lunapaint | ? | AROS ships a descriptor but no class. Find out what the format is before starting. |
| 14 | SVG | `svg` | AROS ships a descriptor but no class. **Blocked:** needs a renderer, and third-party code can't be vendored. |
| 15 | AVIF | `avif` | AROS ships a descriptor but no class. **Blocked:** needs AV1 decoding. |
| 16 | OTB | `otb` | Nokia over-the-air bitmap, 1-bit. A sibling of WBMP. |
| 17 | MSP | `msp` | Microsoft Paint 1 and 2, 1-bit; v2 is RLE. |
| 18 | XV thumbnail | `xvthumb` | `P7 332` header, 8-bit 3:3:2 colour. |
| 19 | GIMP brush and pattern | `gimp` | GBR (grayscale or RGBA) and PAT; one class, two descriptors. |
| 20 | MTV and QRT | `mtv` | Ray-tracer output: plain RGB with a small header. |
| 21 | HRZ | `hrz` | Slow-scan TV, fixed 256×240 with no header: weak descriptor. |
| 22 | VICAR | `vicar` | Planetary imaging: text label, several pixel types and orders. |
| 23 | MGR and CMU bitmaps | `mgr` | 1-bit window-manager bitmaps with magic. Check whether one class fits both. |
| 24 | Palm bitmap | `palm` | Several versions, compressions and colour tables. |
| 25 | PlayStation TIM | `tim` | 4, 8, 16 and 24-bit, with colour tables (CLUTs). |
| 26 | DPX and Cineon | `dpx` | Film scans: 10-bit packed data, both byte orders. |
| 27 | Photo CD | `pcd` | Base resolutions first; higher ones need Huffman residuals. Multi-image. |
| 28 | CCITT fax | `fax` | Raw G3 and G4, then CALS on the same decoder. |
| 29 | BLP | `blp` | Blizzard textures. BLP2 uses DDS's BC compression; do it after DDS. |
| 30 | PICT | `pict` | Mac QuickDraw, bitmap opcodes only; ignore vector drawing. |
| 31 | XCF | `xcf` | GIMP, flattened. **Blocked** on the zlib wrapper for zlib tiles. |
| 32 | MIFF | `miff` | ImageMagick's own format. Uncompressed first; zlib, bzip2 and LZMA wait on shared wrappers. |
| 33 | NEOchrome | `neo` | Atari ST, 320×200 in 16 colours. |
| 34 | Spectrum 512 | `spectrum` | Atari ST SPU and compressed SPC. |
| 35 | Compressed Degas | ? | PC1–PC3. Check first whether AROS's stock Degas class reads them; if it does, drop this entry. |

Not queued yet: animation formats (FLI/FLC, MNG) need `animation.datatype` rules the skill doesn't cover. Large-library formats (JBIG, JPEG 2000, JPEG-LS, JPEG XR, camera RAW, EXR, WMF/EMF, EPS/PDF) are out of scope.
