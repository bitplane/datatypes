# MacPaint

Reads MacPaint files, versions 0, 2 and 3, with or without a 128-byte MacBinary header, as 576×720 one-plane pictures in black on white.

Saves version 0 MacPaint. Dark pixels become black, smaller pictures are padded with white, and larger ones keep only their top-left 576×720.

MacPaint has no magic number, so our `MACPAINT` descriptor matches the leading zero byte on files named `.mac`, `.macp`, `.pntg` or `.pnt`, at priority -10. It doesn't recognise MacBinary files named `.bin`.
