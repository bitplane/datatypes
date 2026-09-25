# Format queue

Take the first entry that is not done, claimed or blocked. See `SKILL.md`, section 1, for how to tell whether an entry is claimed, and **Fit** in section 3 for what belongs here.

Done: Targa, PCX, QOI, WBMP, SGI, Sun Raster, XBM, Farbfeld, MacPaint, XPM, PAM and PFM, XWD, OTB, MSP, NEOchrome, XV thumbnail, PlayStation TIM, ICNS, CCITT fax, Palm bitmap, DCX, MGR and CMU bitmaps.

In review when this list was last updated (check `gh pr list` for the current state): ICO and CUR, DDS, MTV and QRT, Spectrum 512.

Stock AROS classes (don't add these): BMP, GIF, ILBM, JPEG, PNG, PNM (P1–P6), TIFF, WebP, HEIC, JPEG XL, Degas, GEM IMG.

| # | Format | Name | Notes |
|---|---|---|---|
| 1 | SIXEL | `sixel` | DEC terminal graphics, still in use. Text-encoded palette raster. IM reads and writes it. |
| 2 | CALS type 1 | `cals` | A G4 fax image behind a text header. Reuse the fax codec through `common/` (its second user). |
| 3 | TIM2 | `tim2` | PlayStation 2 textures, the successor to TIM. Multi-image (several pictures and mipmaps per file). Share code with `tim` through `common/` where it fits. |
| 4 | Utah RLE | `utahrle` | Utah Raster Toolkit RLE. IM and netpbm. |
| 5 | Alias/Wavefront RLA and PIX | `alias` | RLE render output; RLA from Wavefront, PIX from Alias. 8-bit channels only; deeper variants are unsupported per **Fit**. |
| 6 | ZX Spectrum SCREEN$ | `zxscr` | Exactly 6912 bytes, no magic: weak descriptor like WBMP (size check in the codec, filename pattern, priority -10). |
| 7 | CompuServe RLE | `cis` | 1-bit CompuServe RLE images. netpbm. |
| 8 | Palm Database images | `pdb` | Palm ImageViewer and eDoc image records (IM's PDB, netpbm's pdbimg). Share code with `palm` where it fits. |
| 9 | Sun icons | `sunicon` | Text files of hex words. netpbm. |
| 10 | JBIG1 | `jbig` | Bi-level scanner and fax images: QM arithmetic coder, typical prediction, resolution layers. Our own implementation, no library. |
| 11 | FTEX | `ftex` | Civilization texture wrapper around DXT1 or raw data. Pillow. **Blocked** until DDS is merged. |
| 12 | Pixar PXR | `pixar` | Simple raster. Pillow. |
| 13 | AVS and AAI | `avs` | Trivial ARGB (AVS) and RGBA (AAI Dune) rasters. IM. |
| 14 | X cursors (Xcursor) | `xcursor` | ARGB images at several sizes per file, multi-image: default to the largest. Every Linux desktop ships them. |
| 15 | KISS CEL | `kisscel` | Paper-doll cels: 4 and 8-bit with a separate KCF palette file, or 32-bit RGBA. When the palette file is missing, do what GIMP does. |
| 16 | Amiga icons | `info` | `.info` files: OS 1.3 planar images, NewIcons, GlowIcons (OS 3.5 colour chunks) and PNG icons. Normal and selected images are multi-image. |
| 17 | Lunapaint | `lunapaint` | AROS ships a descriptor (`Lunapaint_v1` in UTF-16) but no class. A layered project format: composite every layer the way Lunapaint does, or don't ship. Lunapaint is open source; read it to understand the format, but don't port it. |
| 18 | PSD and PSB | `psd` | Photoshop's stored composite image. RLE; CMYK, Lab and 16-bit need converting. Reject files whose composite isn't usable (saved without "maximise compatibility") rather than showing it blank. Read-only. |
| 19 | OpenRaster and Krita | `ora` | Zip archives whose spec requires `mergedimage.png`, the full composite. Needs zip reading (`common/zlib`) and PNG decoding; ICNS has a PNG decoder, which becomes shared code once this is its second user. |
| 20 | XCF | `xcf` | GIMP. Full compositing only: layers, masks, blend modes and groups. |
| 21 | Paint Shop Pro | `psp` | PSP 3 and later. Layered, so full compositing only: layers, masks, blend modes, groups and vector layers' rasterised form where stored. |
| 22 | Photo CD | `pcd` | Every resolution, including the Huffman-coded higher ones; the largest is the default image. |
| 23 | BLP | `blp` | Blizzard textures. BLP2 uses DDS's BC compression. **Blocked** until DDS is merged. |
| 24 | IFF variants | ? | DEEP, RGBN/RGB8, ACBM, PBM and 24-bit ILBM. Check first which ones AROS's stock ILBM class reads; only add what it doesn't. |
| 25 | Compressed Degas | ? | PC1–PC3. Check first whether AROS's stock Degas class reads them; if it does, drop this entry. |
| 26 | GIMP brushes and patterns | `gimp` | GBR (grayscale or RGBA), GIH brush pipes (a sequence of GBR brushes, so multi-image) and PAT; one class, several descriptors. Niche. |
| 27 | PowerVR textures | `pvr` | PVR v3 container: uncompressed and ETC1/ETC2 first; PVRTC is harder. Mipmaps and faces are multi-image. |
| 28 | Arma textures | `paa` | Bohemia Interactive PAA: DXT1–DXT5 and uncompressed variants. **Blocked** until DDS is merged. |
| 29 | AVIF | `avif` | AROS ships a descriptor but no class. **Blocked:** needs AV1 decoding. 8-bit SDR only. |

Not queued: animation formats (FLI/FLC, MNG) need `animation.datatype` rules the skill doesn't cover. Out of scope under **Fit**: HDR, FITS, DPX and Cineon, VICAR, PICT, SVG, MIFF, HRZ, and large-library formats (JPEG 2000, JPEG-LS, JPEG XR, camera RAW, EXR, WMF/EMF, EPS/PDF).
