# KiSS CEL

Reads KiSS paper-doll cels: old headerless 4-bit cels, KiSS/GS 4-bit and 8-bit cels, and Cherry KiSS 32-bit cels. Index 0 is transparent, and the cel's x and y offset becomes transparent space around it.

Palette cels need a KiSS palette file, which KiSS keeps separately. The class looks in the cel's directory for a `.cnf` that lists the cel and names its palette, then a `.kcf` with the cel's name, then the directory's only `.kcf`. Without one, the cel shows in shades of grey.

Saves 32-bit cels, which need no palette file.

Our `KISSCEL` descriptor matches `.cel` files by name at priority -10, since old cels have no magic.
