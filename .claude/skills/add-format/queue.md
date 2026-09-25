# Format queue

Take the first entry that is not done, claimed or blocked. See `SKILL.md`, section 1, for how to tell whether an entry is claimed, and **Fit** in section 3 for what belongs here.

Done: Targa, PCX, QOI, WBMP, SGI, Sun Raster, XBM, Farbfeld, MacPaint, XPM, PAM and PFM, XWD, OTB, MSP, NEOchrome, XV thumbnail, PlayStation TIM, ICNS, CCITT fax (with CALS type 1), Palm bitmap, DCX, MGR and CMU bitmaps, ICO and CUR, DDS, MTV and QRT, Spectrum 512.

In review when this list was last updated (check `gh pr list` for the current state): SIXEL, TIM2, Pixar PXR, Utah RLE, Sun icons, Xcursor, ZX Spectrum SCREEN$, CompuServe RLE, and CALS fixes to the fax class.

Stock AROS classes (don't add these): BMP, GIF, ILBM, JPEG, PNG, PNM (P1–P6), TIFF, WebP, HEIC, JPEG XL, Degas, GEM IMG.

| # | Format | Name | Notes |
|---|---|---|---|
| 1 | Alias/Wavefront RLA and PIX | `alias` | RLE render output; RLA from Wavefront, PIX from Alias. 8-bit channels only; deeper variants are unsupported per **Fit**. |
| 2 | Palm Database images | `pdb` | Palm ImageViewer and eDoc image records (IM's PDB, netpbm's pdbimg). Share code with `palm` where it fits. |
| 3 | JBIG1 | `jbig` | Bi-level scanner and fax images: QM arithmetic coder, typical prediction, resolution layers. Our own implementation, no library. |
| 4 | FTEX | `ftex` | Civilization texture wrapper around DXT1 or raw data. Pillow. **Blocked** until `formats/dds/bcn.[ch]` moves to `common/` in its own PR. |
| 5 | AVS and AAI | `avs` | Trivial ARGB (AVS) and RGBA (AAI Dune) rasters. IM. |
| 6 | KISS CEL | `kisscel` | Paper-doll cels: 4 and 8-bit with a separate KCF palette file, or 32-bit RGBA. When the palette file is missing, do what GIMP does. |
| 7 | Amiga icons | `info` | `.info` files: OS 1.3 planar images, NewIcons, GlowIcons (OS 3.5 colour chunks) and PNG icons. Normal and selected images are multi-image. |
| 8 | Lunapaint | `lunapaint` | AROS ships a descriptor (`Lunapaint_v1` in UTF-16) but no class. A layered project format: composite every layer the way Lunapaint does, or don't ship. Lunapaint is open source; read it to understand the format, but don't port it. |
| 9 | PSD and PSB | `psd` | Photoshop's stored composite image. RLE; CMYK, Lab and 16-bit need converting. Reject files whose composite isn't usable (saved without "maximise compatibility") rather than showing it blank. Read-only. |
| 10 | OpenRaster and Krita | `ora` | Zip archives whose spec requires `mergedimage.png`, the full composite. Needs zip reading (`common/zlib`) and PNG decoding; ICNS has a PNG decoder, which becomes shared code once this is its second user. |
| 11 | XCF | `xcf` | GIMP. Full compositing only: layers, masks, blend modes and groups. |
| 12 | Paint Shop Pro | `psp` | PSP 3 and later. Layered, so full compositing only: layers, masks, blend modes, groups and vector layers' rasterised form where stored. |
| 13 | Photo CD | `pcd` | Every resolution, including the Huffman-coded higher ones; the largest is the default image. |
| 14 | BLP | `blp` | Blizzard textures. BLP2 uses DDS's BC compression. **Blocked** until `formats/dds/bcn.[ch]` moves to `common/` in its own PR. |
| 15 | IFF variants | ? | DEEP, RGBN/RGB8, ACBM, PBM and 24-bit ILBM. Check first which ones AROS's stock ILBM class reads; only add what it doesn't. |
| 16 | Compressed Degas | ? | PC1–PC3. Check first whether AROS's stock Degas class reads them; if it does, drop this entry. |
| 17 | GIMP brushes and patterns | `gimp` | GBR (grayscale or RGBA), GIH brush pipes (a sequence of GBR brushes, so multi-image) and PAT; one class, several descriptors. Niche. |
| 18 | PowerVR textures | `pvr` | PVR v3 container: uncompressed and ETC1/ETC2 first; PVRTC is harder. Mipmaps and faces are multi-image. |
| 19 | Arma textures | `paa` | Bohemia Interactive PAA: DXT1–DXT5 and uncompressed variants. **Blocked** until `formats/dds/bcn.[ch]` moves to `common/` in its own PR. |
| 20 | AVIF | `avif` | AROS ships a descriptor but no class. **Blocked:** needs AV1 decoding. 8-bit SDR only. |

Not queued: animation formats (FLI/FLC, MNG) need `animation.datatype` rules the skill doesn't cover. Out of scope under **Fit**: HDR, FITS, DPX and Cineon, VICAR, PICT, SVG, MIFF, HRZ, and large-library formats (JPEG 2000, JPEG-LS, JPEG XR, camera RAW, EXR, WMF/EMF, EPS/PDF).
