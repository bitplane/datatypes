# TIM2

Reads PlayStation 2 TIM2 textures: 4 and 8-bit indexed with 16, 24 or 32-bit CLUTs, and 16, 24 and 32-bit direct colour, with alpha. Indexed pictures without a CLUT load as grayscale.

Every mipmap level of every picture is a separate image. The first picture at full size loads by default, and `PDTA_WhichPicture` picks another.

Not supported: CLUT-only `CLT2` files. Textures stored in the GS's swizzled order load scrambled.

Saves 24-bit TIM2 for opaque pictures and 32-bit otherwise.

The `TIM2` descriptor matches the `TIM2` magic on files named `.tm2` or `.tim2`.
