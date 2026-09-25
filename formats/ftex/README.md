# FTEX

Reads FTEX textures from Independence War 2: `.ftc` compressed and `.ftu` uncompressed files, in DXT1 or 24-bit RGB. A file can hold several formats, each with its own mip levels, and each is a separate picture. The first loads by default, and `PDTA_WhichPicture` picks another.

Saves an uncompressed `.ftu` file with one 24-bit RGB image, compositing transparency over white.

Our `FTEX` descriptor matches the `FTEX` magic in files named `.ftc` or `.ftu`.
