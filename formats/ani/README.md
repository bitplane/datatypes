# ANI

Reads Windows animated cursors (`.ani`, sometimes named `.cur`). Each frame is an icon or cursor with the same entry types the ICO class reads: BMP at 1 to 32 bits per pixel, or PNG. PNG entries load through `png.datatype` via a temporary file, so they need a writable `T:`.

Frames load as separate pictures, not an animation: the first by default, or another by its position with `PDTA_WhichPicture`. Timing and sequence chunks are ignored. Within a frame the largest entry loads, and the deepest of equal sizes. A cursor's hotspot becomes the picture's grab point.

Not supported: raw bitmap frames without an icon header.

Saves a one-frame ANI holding a cursor up to 256×256: 24-bit if every pixel is opaque, 32-bit with alpha otherwise. The grab point becomes the hotspot.

The `ANI` descriptor matches the `RIFF`…`ACON` header in any file, at priority 1, above AROS's AVI descriptor.
