# Alias/Wavefront RLA and PIX

Reads Wavefront RLA and Alias PIX, two run-length encoded formats from 3D renderers. The class tells them apart by content.

RLA files have grey or RGB channels of 1 to 16 bits, and the first matte channel becomes alpha. A file can chain several images. The first loads by default, and `PDTA_WhichPicture` picks another. PIX files hold 24-bit colour or an 8-bit grey matte.

Saves 8-bit RGB RLA, with a matte channel if the picture has transparency. Doesn't save PIX.

Not supported: float and 32-bit RLA channels, the older RLB layout and 3ds Max RPF files.

Our `Alias` descriptor matches `.rla`, `.pix`, `.als` and `.alias` files by name at priority -10, since PIX has no magic number.
