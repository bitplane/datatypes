# Scitex CT

Reads Scitex HandShake CT (continuous tone) files, the prepress scans written by Scitex systems, Photoshop and many scanner programs. It reads any mix of the cyan, magenta, yellow and black separations and converts them to RGB. That covers CMYK, CMY (used for RGB scans) and black-only grayscale. Images load opaque.

Saves grayscale images as black-only CT, and other images as CMY. Alpha is composited over white.

Rejects the other HandShake types (LW, BM, PG and TX), and files with separations beyond CMYK.

Our `SCT` descriptor matches the `CT` type code at byte 80. That is only two bytes, so the file must also be named `.sct`, `.ct` or `.ch`.
