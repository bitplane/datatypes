# Sun icon

Reads SunView and OpenWindows icon and cursor files: a `/* Format_version=1, ... */` header comment followed by hex items. Depth 1 loads black on white, and depth 8 loads as 256 grey levels, since the file doesn't carry its palette. Items may be 8, 16 or 32 bits wide.

Saves depth 1 with 16-bit items, as Sun's `iconedit` writes it. The width is rounded up to a multiple of 16 with white columns on the right.

Not supported: depths other than 1 and 8, and other `Format_version`s.

Our `SUNICON` descriptor matches files starting with `/* Format_version=1`, whatever their name. Files with another comment before the header still load, but the descriptor doesn't recognise them.
