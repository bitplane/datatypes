# Farbfeld

Reads Farbfeld images: 16-bit big-endian RGBA with straight alpha, rounded to 8 bits per channel.

Saves Farbfeld, widening each 8-bit channel exactly, so a saved image loads back unchanged.

Our `FARBFELD` descriptor matches the `farbfeld` magic, whatever the file's name.
