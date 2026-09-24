# Format queue

Take the first entry that is not done, claimed or blocked. See `SKILL.md`, section 1, for how to tell whether an entry is claimed.

Done: Targa, PCX, QOI, WBMP, SGI, Sun Raster, XBM, Farbfeld, MacPaint, XPM, PAM and PFM, XWD, OTB.

In review when this list was last updated (check `gh pr list` for the current state): ICO and CUR, MSP, NEOchrome, XV thumbnail, PlayStation TIM, ICNS (adds `common/zlib`), CCITT fax, Palm bitmap, DDS.

Stock AROS classes (don't add these): BMP, GIF, ILBM, JPEG, PNG, PNM (P1–P6), TIFF, WebP, HEIC, JPEG XL, Degas, GEM IMG.

| # | Format | Name | Notes |
|---|---|---|---|
| 1 | DCX | `dcx` | Multi-page PCX. May reuse the PCX codec through `common/` once there are two users. |
| 2 | PSD and PSB | `psd` | The flattened composite image only. RLE; CMYK, Lab and 16-bit need converting. Read-only. |
| 3 | Radiance HDR | `hdr` | RGBE with RLE scanlines. Float to 8-bit per `SKILL.md`. |
| 4 | FITS | `fits` | Astronomy data: BITPIX, BSCALE/BZERO, several HDUs. |
| 5 | Lunapaint | ? | AROS ships a descriptor but no class. Find out what the format is before starting. |
| 6 | GIMP brush and pattern | `gimp` | GBR (grayscale or RGBA) and PAT; one class, two descriptors. |
| 7 | MTV and QRT | `mtv` | Ray-tracer output: plain RGB with a small header. |
| 8 | HRZ | `hrz` | Slow-scan TV, fixed 256×240 with no header: weak descriptor. |
| 9 | VICAR | `vicar` | Planetary imaging: text label, several pixel types and orders. |
| 10 | MGR and CMU bitmaps | `mgr` | 1-bit window-manager bitmaps with magic. Check whether one class fits both. |
| 11 | DPX and Cineon | `dpx` | Film scans: 10-bit packed data, both byte orders. |
| 12 | Photo CD | `pcd` | Base resolutions first; higher ones need Huffman residuals. Multi-image. |
| 13 | BLP | `blp` | Blizzard textures. BLP2 uses DDS's BC compression. **Blocked** until DDS is merged. |
| 14 | PICT | `pict` | Mac QuickDraw, bitmap opcodes only; ignore vector drawing. |
| 15 | XCF | `xcf` | GIMP, flattened. **Blocked** until `common/zlib` (in the ICNS PR) is merged. |
| 16 | MIFF | `miff` | ImageMagick's own format. Uncompressed first; zlib once `common/zlib` is merged; bzip2 and LZMA wait on shared wrappers. |
| 17 | Spectrum 512 | `spectrum` | Atari ST SPU and compressed SPC. |
| 18 | Compressed Degas | ? | PC1–PC3. Check first whether AROS's stock Degas class reads them; if it does, drop this entry. |
| 19 | SVG | `svg` | AROS ships a descriptor but no class. **Blocked:** needs a renderer, and third-party code can't be vendored. |
| 20 | AVIF | `avif` | AROS ships a descriptor but no class. **Blocked:** needs AV1 decoding. |

Not queued yet: animation formats (FLI/FLC, MNG) need `animation.datatype` rules the skill doesn't cover. Large-library formats (JBIG, JPEG 2000, JPEG-LS, JPEG XR, camera RAW, EXR, WMF/EMF, EPS/PDF) are out of scope.
