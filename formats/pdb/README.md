# Palm ImageViewer

Reads the image record of Palm ImageViewer databases, `.pdb` files of type `vIMG` and creator `View`: black and white, 4 grays or 16 grays, uncompressed or run-length encoded. FireViewer's grayscale files load too. The note record is ignored.

Not supported: FireViewer's colour and tiled images, compression types 2 to 7, and other image types.

Saves uncompressed images at the smallest depth that holds every gray exactly, or 16 grays otherwise. The width is padded to a multiple of 16 with white, and the database takes the picture's object name.

The `PDB` descriptor matches `vIMGView` at offset 60 on files named `.pdb`.
