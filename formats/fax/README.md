# FAX

Reads raw CCITT Group 3 fax files with one-dimensional (MH) coding, and CALS type 1 rasters, which hold Group 4 data after a text header. Pictures load black on white. Group 3 files may store bits in either order. Only the first page of a multi-page Group 3 file loads. CALS drawings stored on their side show upright.

Saves raw Group 3 MH at the picture's own width. Fax machines expect 1728-pixel lines, so pad the picture to that width before sending it.

Not supported: Group 3 two-dimensional (MR) coding, raw Group 4 files, CALS type 2 tiled rasters, and the Digifax header.

Our `FAX` descriptor matches files named `.g3`, `.fax`, `.cal`, `.cals`, `.ct1`, `.c4`, `.mil` or `.ras`, at priority -10, since the files have no magic. The Sun Raster descriptor still claims Sun Raster files named `.ras` first.
