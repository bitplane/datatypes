# OTB

Reads Nokia OTA bitmaps, the 1-bit format of operator logos and picture messages. Rows may be packed or padded to a byte. Animated bitmaps hold up to 16 pictures: the first loads by default, and `PDTA_WhichPicture` picks another.

Not supported: compressed bitmaps, more than one colour plane, and OTA bitmaps stored as hex text.

Saves a one-picture OTB in black and white, with rows padded to a byte.

The `OTB` descriptor matches files named `.otb` at priority -10, since the format has no magic number.
