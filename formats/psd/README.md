# Photoshop PSD and PSB

Reads the merged composite that Photoshop, GIMP, Krita, ImageMagick and others store in `.psd`, `.pdd` and `.psb` (large document) files. That covers bitmap, grayscale, duotone (shown in grey), indexed, RGB, CMYK, Lab and multichannel images at 1, 8 and 16 bits, raw or RLE. Multichannel images show their first three channels as RGB, or the first in grey.

Layers aren't loaded one by one; the composite shows them merged, with its transparency. Saved selections and spot channels are ignored. Saving isn't supported.

Rejects files saved without "Maximize Compatibility", which hold no real composite, as well as 32-bit files and ZIP-compressed composites.

Our `PSD` and `PSB` descriptors match the `8BPS` signature with version 1 or 2, and need a `.psd`, `.pdd` or `.psb` name.
