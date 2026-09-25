# Format queue

Take the first entry that is not done, claimed or blocked. See `SKILL.md`, section 1, for how to tell whether an entry is claimed, and **Fit** in section 3 for what belongs here.

Done: Targa, PCX, QOI, WBMP, SGI, Sun Raster, XBM, Farbfeld, MacPaint, XPM, PAM and PFM, XWD, OTB, MSP, NEOchrome, XV thumbnail, PlayStation TIM, ICNS, CCITT fax (with CALS type 1), Palm bitmap, DCX, MGR and CMU bitmaps, ICO and CUR, DDS, MTV and QRT, Spectrum 512, SIXEL, TIM2, Pixar PXR, Utah RLE, Sun icons, Xcursor, ZX Spectrum SCREEN$, CompuServe RLE, Alias/Wavefront RLA and PIX, AVS and AAI, Palm Database images, Amiga icons.

Stock AROS classes (don't add these): BMP, GIF, ILBM, JPEG, PNG, PNM (P1–P6), TIFF, WebP, HEIC, JPEG XL, Degas (including compressed PC1–PC3), GEM IMG.

Many retro entries come from RECOIL. RECOIL is GPL, so use it to check a format, but don't copy code from it. The magic numbers in these notes are from memory; confirm them before relying on them. Formats with no magic are matched by extension and exact file size; check that their descriptors don't collide with each other.

| # | Format | Name | Notes |
|---|---|---|---|
| 1 | KISS CEL | `kisscel` | Paper-doll cels: 4 and 8-bit with a separate KCF palette file, or 32-bit RGBA. When the palette file is missing, do what GIMP does. |
| 2 | FTEX | `ftex` | Civilization texture wrapper around DXT1 (`common/bcn.h`) or raw data. Pillow. |
| 3 | Arma textures | `paa` | Bohemia Interactive PAA: DXT1–DXT5 and uncompressed variants. |
| 4 | BLP | `blp` | Blizzard textures. BLP2 uses BC compression from `common/bcn.h`. |
| 5 | ZX Spectrum extended screens | `zxscr` | Extend the existing class: ULAplus, Timex hi-colour and hi-res, multicolour, IFL, Gigascreen, border screens and SXG. File size tells most of them apart. The README lists these as unsupported now. |
| 6 | Falcon and TT true colour | `falcon` | GodPaint, TRU, COKE TG1, TCP, EggPaint, Prism Paint, DuneGraph and TT PI4–PI6. Mostly RGB565, and most have magic numbers. |
| 7 | Atari ST raw screens | `stscreen` | Art Director, Doodle, Graphics Processor, Paintworks SC0–SC2, EZ-Art, ComputerEyes and similar. Trivial bitplane dumps, mostly with no magic, so match on extension and file size. |
| 8 | PSD and PSB | `psd` | Photoshop's stored composite image. RLE; CMYK, Lab and 16-bit need converting. Reject files whose composite isn't usable (saved without "maximise compatibility") rather than showing it blank. Read-only. |
| 9 | IFF variants | ? | Multi-palette ILBM (SHAM, PCHG, CTBL/Dynamic HiRes, BEAM), plus DEEP, RGBN/RGB8, ACBM, PBM and 24-bit ILBM. Stock ILBM reads HAM6, HAM8 and EHB but not the multi-palette chunks; check what else it reads and only add what it doesn't. The descriptor has to win over stock ILBM for files it can't show properly. |
| 10 | Atari ST compressed paint | `stpaint` | Tiny, CrackArt, Imagic, STAD PAC, Paintworks CL0–CL2, Dali and Pablo. A different RLE per program. The ST palette scaling is `neo`'s, so this second user moves it to `common/`. |
| 11 | C64 bitmaps | `c64` | Koala, Art Studio, Advanced Art Studio, Doodle, Amica, Drazpaint, FLI/AFLI and about ten more. Needs a built-in palette (Pepto or Colodore). Multicolour pixels are double width. |
| 12 | OpenRaster and Krita | `ora` | Zip archives whose spec requires `mergedimage.png`, the full composite. Needs zip reading (`common/zlib`) and PNG decoding; ICNS has a PNG decoder, which becomes shared code once this is its second user. |
| 13 | GIMP brushes and patterns | `gimp` | GBR (grayscale or RGBA), GIH brush pipes (a sequence of GBR brushes, so multi-image) and PAT; one class, several descriptors. Niche. |
| 14 | PowerVR textures | `pvr` | PVR v3 container: uncompressed and ETC1/ETC2 first; PVRTC is harder. Mipmaps and faces are multi-image. |
| 15 | Japanese PC pictures | `japanpc` | Maki-chan MAG and MKI, Pi, X68000 PIC. Good magic numbers and large archives of art. Pixels aren't square; scale to the intended aspect. |
| 16 | Atari ST multi-palette | `stmulti` | MPP and PhotoChrome PCS; Spectrum SPX goes into `spectrum` instead. Some files are two frames meant to be blended; show the blend. |
| 17 | C64 interlace | `c64` | Extend the C64 class: Drazlace, Gunpaint, MUIFLI, True Paint and others. Two frames blended, as they were meant to be seen. |
| 18 | MSX2 screens | `msx` | SC2, SC5, SC7, SC8, YJK SCA/SCC and Graph Saurus. Weak headers; the palette comes from the VRAM dump or the MSX2 default. |
| 19 | JBIG1 | `jbig` | Bi-level scanner and fax images: QM arithmetic coder, typical prediction, resolution layers. Our own implementation, no library. |
| 20 | Lunapaint | `lunapaint` | AROS ships a descriptor (`Lunapaint_v1` in UTF-16) but no class. A layered project format: composite every layer the way Lunapaint does, or don't ship. Lunapaint is open source; read it to understand the format, but don't port it. |
| 21 | XCF | `xcf` | GIMP. Full compositing only: layers, masks, blend modes and groups. |
| 22 | Paint Shop Pro | `psp` | PSP 3 and later. Layered, so full compositing only: layers, masks, blend modes, groups and vector layers' rasterised form where stored. |
| 23 | Photo CD | `pcd` | Every resolution, including the Huffman-coded higher ones; the largest is the default image. |
| 24 | Quake 2 WAL | `wal` | 8-bit textures in Quake 2's fixed palette (Pillow builds it in). The four mip levels are multi-image. |
| 25 | GD and GD2 | `gd` | libgd's raw formats. GD is a palette or truecolour dump; GD2 adds zlib-compressed chunks (`common/zlib`). |
| 26 | Dr. Halo CUT | `cut` | Simple RLE, with the palette in a separate `.pal` file. When it's missing, fall back the way KISS CEL does. |
| 27 | Scitex CT | `sct` | Prepress scans: uncompressed CMYK or gray, converted to RGB. |
| 28 | Khoros VIFF | `viff` | A header then bands of raw data. Byte data only; float and complex bands fail **Fit**. |
| 29 | Windows ANI cursors | `ani` | RIFF chunks wrapping ICO frames; reuse `ico`'s code. Treat it as a multi-image file of frames, not an animation. |
| 30 | AVIF | `avif` | AROS ships a descriptor but no class. **Blocked:** needs AV1 decoding. 8-bit SDR only. |

Not queued: animation formats (FLI/FLC, MNG) need `animation.datatype` rules the skill doesn't cover. JPEG wrappers (JNG, MPO, FlashPix, SFW) need a memory-source JPEG datatype, which AROS doesn't have. BPG is blocked on HEVC, like AVIF. From RECOIL: Atari 8-bit formats (almost no magic and no standard palette), text-mode pictures (need copyrighted character ROMs), fonts and sprite sheets, and formats split across several files. Out of scope under **Fit**: HDR, FITS, DPX and Cineon, VICAR, PICT, SVG, MIFF, HRZ, DICOM and other science formats, and large-library formats (JPEG 2000, JPEG-LS, JPEG XR, camera RAW, EXR, WMF/EMF, EPS/PDF).
