# CompuServe RLE

Reads CompuServe RLE (VIDTEX) pictures: `ESC G M` for 128×96 and `ESC G H` for 256×192.

Saves 128×96 when the picture fits, and 256×192 otherwise, cropping larger pictures and padding smaller ones with white.

Not supported: the 640×200 `ESC G S` mode.

Our `CIS` descriptor matches `ESC G` in files named `.rle` or `.cis`.
