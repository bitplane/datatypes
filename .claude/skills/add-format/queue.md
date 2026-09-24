# Format queue

Take the first entry that is not done, claimed or blocked. See `SKILL.md`, section 1, for how to tell whether an entry is claimed.

Done: Targa, PCX, QOI, WBMP, SGI, Sun Raster. In progress: XBM.

Stock AROS classes (don't add these): BMP, GIF, ILBM, JPEG, PNG, PNM (P1–P6), TIFF, WebP, HEIC, JPEG XL, Degas, GEM IMG.

| # | Format | Name | Notes |
|---|---|---|---|
| 1 | XPM | `xpm` | X11 pixmaps, a text format. Supports XPM3 and XPM2. Colour names need an X11 colour table of our own. |
| 2 | Farbfeld | `farbfeld` | 16-bit RGBA with a magic number. |
| 3 | PAM and PFM | `pam` | Netpbm P7 plus PFM (`PF`, `Pf`, and Pillow's `PF4`). A new basename next to the stock `pnm`. PFM is float data. |
| 4 | XWD | `xwd` | X window dumps. Big-endian header, visual classes, colour maps. |
| 5 | MacPaint | `macpaint` | PackBits, 576×720, no magic. May have a MacBinary header. |
| 6 | ICO and CUR | `ico` | Multi-image: largest and deepest by default. Entries may be BMP or PNG. **Blocked** on the zlib wrapper for PNG entries. |
| 7 | DCX | `dcx` | Multi-page PCX. May reuse the PCX codec through `common/` once there are two users. |
| 8 | DDS | `dds` | Uncompressed, BC1–BC5, then BC6H and BC7. Mipmaps and cube faces are multi-image. |
| 9 | PSD and PSB | `psd` | The flattened composite image only. RLE; CMYK, Lab and 16-bit need converting. Read-only. |
| 10 | Radiance HDR | `hdr` | RGBE with RLE scanlines. Float to 8-bit per `SKILL.md`. |
| 11 | FITS | `fits` | Astronomy data: BITPIX, BSCALE/BZERO, several HDUs. |
| 12 | ICNS | `icns` | Apple icons: packed RLE entries plus PNG and JPEG 2000. **Blocked** on the zlib wrapper; JPEG 2000 entries are unsupported. |
| 13 | Lunapaint | ? | AROS ships a descriptor but no class. Find out what the format is before starting. |
| 14 | SVG | `svg` | AROS ships a descriptor but no class. **Blocked:** needs a renderer, and third-party code can't be vendored. |
| 15 | AVIF | `avif` | AROS ships a descriptor but no class. **Blocked:** needs AV1 decoding. |
