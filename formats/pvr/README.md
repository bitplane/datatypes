# PowerVR PVR

Reads PowerVR textures from PVRTexTool, Apple's `texturetool`, cocos2d and the PowerVR SDK, in version 3 files and in version 2 files (`PVR!`). Compressed images can be PVRTC 2 and 4 bpp, ETC1, ETC2 (RGB, RGBA, punch-through), EAC R11 and RG11, DXT1 to DXT5, BC4, BC5 and BC7. Uncompressed images can be any mix of red, green, blue, alpha, luminance and intensity channels of up to 32 bits each, including packed 4444, 5551 and 565, and twiddled version 2 data.

Mip levels, cube faces, array surfaces and depth slices are separate pictures. The first loads by default, and `PDTA_WhichPicture` picks another. Mirrored and bottom-up textures load upright.

Saves uncompressed version 3 PVR: 8-bit RGB when every pixel is opaque, RGBA otherwise.

Not supported: PVRTC-II, ASTC, Basis, BC6, YUV, shared-exponent and other float formats, signed channels, and big-endian files.

Our `PVR` and `PVR v2` descriptors match each version's header on files named `.pvr`. Sega Dreamcast `.pvr` files are a different format and aren't read.
