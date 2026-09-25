# Format queue

Take the first entry that is not done, claimed or blocked. See `SKILL.md`, section 1, for how to tell whether an entry is claimed, and **Fit** in section 3 for what belongs here.

Done: Targa, PCX, QOI, WBMP, SGI, Sun Raster, XBM, Farbfeld, MacPaint, XPM, PAM and PFM, XWD, OTB, MSP, NEOchrome, XV thumbnail, PlayStation TIM, ICNS, CCITT fax (with CALS type 1), Palm bitmap, DCX, MGR and CMU bitmaps, ICO and CUR, DDS, MTV and QRT, Spectrum 512, SIXEL, TIM2, Pixar PXR, Utah RLE, Sun icons, Xcursor, ZX Spectrum SCREEN$, CompuServe RLE, Alias/Wavefront RLA and PIX, AVS and AAI, Palm Database images, Amiga icons, FTEX, Arma PAA, BLP, ZX Spectrum extended screens, Falcon and TT true colour, Atari ST compressed paint, C64 bitmaps and interlace, Japanese PC pictures, KISS CEL, Atari ST raw screens, PowerVR textures, Atari ST multi-palette, JBIG1, Lunapaint, GD and GD2, Dr. Halo CUT, Scitex CT, Khoros VIFF.

Stock AROS classes (don't add these): BMP, GIF, ILBM, JPEG, PNG, PNM (P1–P6), TIFF, WebP, HEIC, JPEG XL, Degas (including compressed PC1–PC3), GEM IMG.

Many retro entries come from RECOIL. RECOIL is GPL, so use it to check a format, but don't copy code from it. The magic numbers in these notes are from memory; confirm them before relying on them. Formats with no magic are matched by extension and exact file size; check that their descriptors don't collide with each other.

| # | Format | Name | Notes |
|---|---|---|---|
| 1 | PSD and PSB | `psd` | Photoshop's stored composite image. RLE; CMYK, Lab and 16-bit need converting. Reject files whose composite isn't usable (saved without "maximise compatibility") rather than showing it blank. Read-only. |
| 2 | IFF variants | ? | Multi-palette ILBM (SHAM, PCHG, CTBL/Dynamic HiRes, BEAM), plus DEEP, RGBN/RGB8, ACBM, PBM and 24-bit ILBM. Stock ILBM reads HAM6, HAM8 and EHB but not the multi-palette chunks; check what else it reads and only add what it doesn't. The descriptor has to win over stock ILBM for files it can't show properly. |
| 3 | OpenRaster and Krita | `ora` | Zip archives whose spec requires `mergedimage.png`, the full composite. Needs zip reading (`common/zlib`) and PNG decoding; ICNS has a PNG decoder, which becomes shared code once this is its second user. |
| 4 | MSX2 screens | `msx` | SC2, SC5, SC7, SC8, YJK SCA/SCC and Graph Saurus. Weak headers; the palette comes from the VRAM dump or the MSX2 default. |
| 5 | XCF | `xcf` | GIMP. Full compositing only: layers, masks, blend modes and groups. |
| 6 | Paint Shop Pro | `psp` | PSP 3 and later. Layered, so full compositing only: layers, masks, blend modes, groups and vector layers' rasterised form where stored. |
| 7 | Photo CD | `pcd` | Every resolution, including the Huffman-coded higher ones; the largest is the default image. |
| 8 | Quake 2 WAL | `wal` | 8-bit textures in Quake 2's fixed palette (Pillow builds it in). The four mip levels are multi-image. |
| 9 | Windows ANI cursors | `ani` | RIFF chunks wrapping ICO frames; reuse `ico`'s code. Treat it as a multi-image file of frames, not an animation. |
| 10 | AVIF | `avif` | AROS ships a descriptor but no class. **Blocked:** needs AV1 decoding. 8-bit SDR only. |

Not queued: animation formats (FLI/FLC, MNG) need `animation.datatype` rules the skill doesn't cover. JPEG wrappers (JNG, MPO, FlashPix, SFW) need a memory-source JPEG datatype, which AROS doesn't have. BPG is blocked on HEVC, like AVIF. From RECOIL: Atari 8-bit formats (almost no magic and no standard palette), text-mode pictures (need copyrighted character ROMs), fonts and sprite sheets, and formats split across several files. Out of scope under **Fit**: HDR, FITS, DPX and Cineon, VICAR, PICT, SVG, MIFF, HRZ, DICOM and other science formats, and large-library formats (JPEG 2000, JPEG-LS, JPEG XR, camera RAW, EXR, WMF/EMF, EPS/PDF).
