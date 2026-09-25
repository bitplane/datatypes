# PAM and PFM

Reads Netpbm PAM (`P7`) and the float maps PFM (`PF` colour, `Pf` gray, `PF4` RGBA) and PHM (`PH`, `Ph`, half floats). PAM covers the tuple types `BLACKANDWHITE`, `GRAYSCALE`, `RGB` and `CMYK`, each with or without `_ALPHA`, at any maxval up to 65535. A file can hold several images, one after another. `PDTA_WhichPicture` picks one, and `PDTA_GetNumPictures` reports how many there are.

Saves 8-bit PAM as `GRAYSCALE` or `RGB`, adding `_ALPHA` when pixels have transparency.

Not supported: PNM images (`P1` to `P6`), which AROS's `pnm` class handles, XV thumbnails (`P7 332`), and float map headers with comments or CRLF line ends.

Our `PAM` descriptor matches `.pam`, `.pfm` and `.phm` files starting with `P`, at priority -1, so AROS's PNM descriptors still take `P1` to `P6`.
