# Photo CD

Reads Kodak Photo CD image packs (`IMG0001.PCD` and the like) and overview packs (`OVERVIEW.PCD`).

An image pack holds every resolution scanned, in this order: 192×128, 384×256, 768×512 and, where the disc has them, the Huffman-coded 1536×1024 (4Base) and 3072×2048 (16Base). The largest loads by default, and `PDTA_WhichPicture` picks another. Portrait scans are turned upright. An overview pack is a set of 192×128 thumbnails, one picture each, the first by default.

PhotoYCC is converted to RGB with Kodak's matrix. Highlights brighter than white are clipped.

Saves nothing.

Not supported: 64Base (6144×4096), which Pro Photo CD keeps in separate `IPE` files.

Our `Photo CD` descriptor matches files named `.pcd` at priority -10, because an image pack's signature is 2 KB into the file. The class rejects other `.pcd` files, such as point clouds. `Photo CD overview` matches overview packs by their signature.
