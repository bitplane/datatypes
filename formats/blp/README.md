# BLP

Reads Blizzard BLP textures from Warcraft III (BLP1) and World of Warcraft (BLP2): palettized, DXT1, DXT3, DXT5 or uncompressed, with or without alpha.

Each mip level is a picture, largest first. The full-size picture loads by default, and `PDTA_WhichPicture` picks another.

Saves BLP2 with one level, palettized if the picture has at most 256 colours.

Not supported: JPEG-compressed BLP, which most Warcraft III textures use, and BLP0.

Our `BLP` descriptor matches the `BLP` magic on `.blp` files.
