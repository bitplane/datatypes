# MGR

Reads 1-bit bitmaps from the MGR window system: the current `yz` layout with depth 1, and the older `zz` and `xz` layouts.

Saves `yz` with depth 1.

Not supported: colour MGR pixmaps, `yz` with depth 8 or `zy`, because they index a palette the file doesn't carry.

Our `MGR` descriptor matches on content only, since MGR files usually have no extension: `yz` followed by the depth byte for 1 (`!`), at priority -1. The decoder reads old `zz` and `xz` files, but the descriptor doesn't recognise them, so MultiView won't open them.
