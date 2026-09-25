# Japanese PC pictures

Reads the compressed picture formats of 1990s Japanese computers: the NEC PC-98 and PC-88, Sharp X68000, FM TOWNS and MSX. Files in a MacBinary wrapper also load. Pictures with non-square pixels are scaled to the right shape by repeating lines or columns.

| Format | Extension | Picture |
|---|---|---|
| MAG, Maki-chan 2 | `.mag` `.max` | 16 or 256 colours, and MSX screen modes 5 to 12 |
| MKI, Maki-chan 1 | `.mki` | 640×400, 16 colours |
| Pi | `.pi` | 16 or 256 colours |
| PIC | `.pic` | X68000, PC-88VA, FM TOWNS and Macintosh, 16 colours to 16M |

Saves MAG in 16 or 256 colours. Pictures with more colours can't be saved.

Not supported: MKI files other than 640×400, Pi pictures 1 or 2 pixels wide, and PIC files of other models or colour depths.

Our `JAPANPC` descriptor matches the extensions above by name at priority -10. The class rejects other `.pic` files, such as Softimage or PC Paint pictures.
