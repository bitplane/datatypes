# MTV and QRT

Reads the output of the MTV and QRT ray tracers, telling them apart by content. Images load opaque. An MTV file can hold several images one after another: the first loads by default, and `PDTA_WhichPicture` picks another.

Saves one MTV image.

The `MTV` descriptor covers both formats. Neither has a magic number, so it matches files named `.mtv`, `.pic`, `.qrt` or `.dis` at priority -10, and the class rejects files that are neither. QRT's own `.raw` name is too generic to claim.
