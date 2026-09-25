# AVS and AAI

Reads two headerless 32-bit formats with alpha: Stardent AVS X images and Dune HD AAI images. The class tells them apart by content. A file can hold several images back to back. The first loads by default, and `PDTA_WhichPicture` picks another.

Saves one image with its alpha, as AAI if the picture came from a `.aai` file and AVS otherwise. An AVS image with zero alpha everywhere loads opaque, so a fully transparent picture saved as AVS reloads opaque.

Our `AVS` descriptor matches `.avs`, `.x` and `.aai` files by name at priority -10, since neither format has a magic number.
