# GD and GD2

Reads the native formats of the libgd graphics library, used by PHP's GD extension and older web tools.

- `.gd`: GD 1.x palette files and GD 2.x palette or truecolour files.
- `.gd2`: versions 1 and 2, palette or truecolour, raw or zlib-compressed chunks.

Palette alpha, truecolour alpha and the transparent colour all load as alpha.

Saves GD 2.x truecolour. gd keeps only 7 bits of alpha, so partly transparent pixels can come back one step off. Opaque and fully transparent pixels are exact.

Rejects GD 2.0 truecolour files from before 2.0.12, which libgd itself no longer reads, and GD2 files whose chunks don't cover the whole image.

Our `GD2` descriptor matches the `gd2` magic on files named `.gd2`. GD files have no magic, so our `GD` descriptor matches `.gd` files by name at priority -10.
