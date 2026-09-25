# NEOchrome

Reads Atari ST NEOchrome pictures in all three modes: low (320×200, 16 colours), medium (640×200, 4 colours) and high (640×400, black and white). STE palettes load with 4 bits per gun. Colour-cycling data is ignored.

Saves NEOchrome when the picture is one of the three screen sizes and fits that mode's palette exactly with ST or STE levels. Other pictures can't be saved.

The `NEO` descriptor matches the zero flag word and the high byte of the resolution on files named `.neo`, at priority -10.
