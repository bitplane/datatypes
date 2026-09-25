# DDS

Reads DirectDraw Surface textures. Compressed images can be DXT1, DXT3, DXT5, ATI1, ATI2 and BC5, and DX10 headers add BC1 to BC7. Uncompressed images can use RGB, luminance or alpha-only bit masks, 8-bit palettes, or the DX10 formats R8G8B8A8, B8G8R8A8, B8G8R8X8 and R10G10B10A2.

Mip levels, cube faces, array slices and volume slices are separate pictures. The first loads by default, and `PDTA_WhichPicture` picks another.

Saves uncompressed DDS with no mip levels: 24-bit RGB when every pixel is opaque, 32-bit ARGB otherwise.

Not supported: DXT2, DXT4, signed BC4, RXGB, YUV, float and 16-bit-per-channel formats, and other DX10 formats, including the `_SRGB` codes of BC1 to BC3 and B8G8R8A8.

Our `DDS` descriptor matches the `DDS ` magic and 124-byte header size in files named `.dds`.
