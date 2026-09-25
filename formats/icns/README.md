# ICNS

Reads Apple icon files: PNG entries, 24-bit icons with 8-bit masks, ARGB entries, and classic 1, 4 and 8-bit icons with 1-bit masks. PNG entries need `z1.library`. Nested icon sets such as dark mode are ignored.

The largest image loads, then the deepest, and a 1x entry wins over a 2x entry of the same pixel size. `PDTA_WhichPicture` picks another.

Not supported: JPEG 2000 entries. They are skipped, so a file holding only JPEG 2000 doesn't load.

Saves a one-image file when the picture is square: 24-bit with an 8-bit mask at 16, 32, 48 and 128 pixels, and PNG at 64, 256, 512 and 1024 pixels.

The `ICNS` descriptor matches the `icns` magic on files named `.icns`.
