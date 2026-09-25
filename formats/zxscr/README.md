# ZX Spectrum screen

Reads ZX Spectrum screens and the extended formats built on them, telling most apart by file size.

| Kind | Extensions |
|---|---|
| Standard, bitmap-only, ULAplus, Timex hi-colour and hi-res | `.scr` |
| Multicolour 8×1 and IFL 8×2 | `.mc` `.mlt` `.ifl` |
| Border screens | `.bsc` `.bmc4` |
| Gigascreen and hi-res Gigascreen | `.img` `.hrg` |
| MultiArtist Gigascreen | `.mg1` `.mg2` `.mg4` `.mg8` |
| Speccy eXtended Graphics | `.sxg` |

Not supported: packed SXG, `.3` and `.rgb` tricolour files, `+3DOS` headers and `.atr` files.

Saves a standard screen if every 8×8 cell fits Spectrum colour limits, otherwise Timex hi-colour if every 8×1 span does.

Our `ZXSCR` descriptor matches the extensions above at priority -11, one below GEM IMG. A Gigascreen `.img` whose bytes 0, 2, 4 and 6 are zero goes to GEM IMG and won't open.
