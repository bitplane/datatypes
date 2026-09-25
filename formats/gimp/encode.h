#ifndef BITPLANE_GIMP_ENCODE_H
#define BITPLANE_GIMP_ENCODE_H
#include <stddef.h>
#include <stdint.h>

/* Flags gimp_scan_row gathers about a picture. */
#define GIMP_SCAN_ALPHA 1u   /* some pixel isn't opaque */
#define GIMP_SCAN_COLOUR 2u  /* some pixel isn't grey */

/* Room for any header below, name included. */
#define GIMP_ENCODED_HEADER 1024u
/* Names longer than this are cut short. */
#define GIMP_MAX_NAME 200u

void gimp_scan_row(const uint8_t *rgba, unsigned width, unsigned *flags);

/* Bytes per pixel that store a picture with these flags losslessly:
   a grey mask or RGBA for a brush, 1 to 4 for a pattern. */
unsigned gbr_bytes(unsigned flags);
unsigned pat_bytes(unsigned flags);

/* Headers for a brush, a one-cell brush pipe (the cell's brush header
   included) or a pattern. name may be NULL. They return the header length,
   or 0 if the size can't be stored. */
size_t gbr_make_header(unsigned width, unsigned height, unsigned bytes,
                       const char *name, uint8_t *out);
size_t gih_make_header(unsigned width, unsigned height, unsigned bytes,
                       const char *name, uint8_t *out);
size_t pat_make_header(unsigned width, unsigned height, unsigned bytes,
                       const char *name, uint8_t *out);

/* One row of width * bytes output bytes. A 1-byte brush stores the mask,
   so grey is inverted; a 1-byte pattern stores grey as it is. */
void gbr_encode_row(const uint8_t *rgba, unsigned width, unsigned bytes,
                    uint8_t *out);
void pat_encode_row(const uint8_t *rgba, unsigned width, unsigned bytes,
                    uint8_t *out);

/* A one-layer XCF (version 0, RLE): the header, layer and tile tables,
   then the tiles, 64x64 in rows, then XCF_TRAILER zero bytes. bytes is 3
   (RGB) or 4 (RGBA). */
size_t xcf_header_size(unsigned width, unsigned height);
/* sizes holds each tile's encoded size, as xcf_encode_tiles gives them.
   Returns the header length (xcf_header_size), or 0 if the size can't be
   stored. */
size_t xcf_make_header(unsigned width, unsigned height, unsigned bytes,
                       const uint32_t *sizes, uint8_t *out);
/* Encode the tiles of one tile row: rgba holds rows RGBA rows (64, or
   fewer for the last) of width pixels. Returns the bytes encoded, put in
   out unless it is NULL; sizes gets each tile's share unless NULL. out
   needs XCF_BAND_BOUND bytes. */
size_t xcf_encode_tiles(const uint8_t *rgba, unsigned width, unsigned rows,
                        unsigned bytes, uint8_t *out, uint32_t *sizes);
#define XCF_BAND_BOUND(width, bytes) ((size_t)(width) * 64u * (bytes) * 2u + 64u)

/* Zero bytes to end the file with: ImageMagick reads the last tile as if
   it could be compressed to its largest size, 1.5 tiles. */
#define XCF_TRAILER(bytes) (64u * 64u * (bytes) * 3u / 2u)
#endif
