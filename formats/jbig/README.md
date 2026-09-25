# JBIG

Reads JBIG1 (ITU-T T.82, ISO 11544) bi-level images, as written by JBIG-KIT, netpbm's `pnmtojbig` and ImageMagick. That covers sequential and progressive files with any number of resolution layers, every stripe order, typical and deterministic prediction (including private DP tables), adaptive template moves, stripe resets and the NEWLEN marker. One-plane pictures load black on white. Files with up to 16 bit-planes load as greyscale, reading the planes as Gray code the way `pnmtojbig` writes them.

Saves one plane in one sequential stripe, with typical prediction. Pixels are composited over white, then set black if their luminance is under half.

Not supported: files of more than 16 planes, and a BIE that continues an earlier one (DL above 0). A file of several BIEs shows the first.

Our `JBIG` descriptor matches `.jbg`, `.jbig` and `.bie` files whose header starts with the fixed zero bytes, at priority -10.
