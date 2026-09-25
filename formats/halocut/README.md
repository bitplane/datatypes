# Dr. Halo CUT

Reads `.cut` pictures from Media Cybernetics' Dr. Halo paint program for DOS, and from other programs that save the format. Pictures are usually 8-bit; 4-bit and 1-bit files are read too.

The colours live in a Dr. Halo palette file with the same name and a `.pal` extension, in the same directory. Without one, the picture shows as a grey ramp, or in black and white if it only uses colours 0 and 1.

Doesn't save: a CUT file can't hold colours without its separate palette.

Rejects files whose third header word isn't zero.

Our `HALOCUT` descriptor matches files named `.cut` whose third header word is zero, at priority -10, since CUT files have no magic.
