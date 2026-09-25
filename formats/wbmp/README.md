# WBMP

Reads type 0 WBMP (WAP bitmap), the 1-bit black and white format.

Saves type 0 WBMP. Pixels are composited over white, then set white if their luminance is at least half.

WBMP has no magic number, so our `WBMP` descriptor matches the two leading zero bytes on files named `.wbmp`, at priority -10.
