# AROS datatypes

One datatype class per directory under `formats/`, each built, versioned, tagged and released on its own. Most sessions add one new format. Keep that work inside the format's directory unless a shared change is genuinely needed.

## Layout

```
formats/<name>/<name>.conf   genmodule config: basename, version, date, superclass, methods
formats/<name>/<name>class.c AROS glue: <Basename>__OM_NEW, <Basename>__DTM_WRITE, ...
formats/<name>/*.c *.h       pure C99 codec: no AROS headers, host-testable
formats/<name>/<NAME>.dtyp   compiled descriptor, only if AROS doesn't ship one
tests/<name>.c               host test for the codec (assert-based, run under ASan/UBSan)
common/                      shared code, opted into per format by #include
```

Nothing lists the formats. `make test`, `scripts/build.sh`, `scripts/release.sh` and the CI matrix all find them from `formats/*`, so a new directory plus `tests/<name>.c` is picked up everywhere.

## Codecs

- C99, `-Wall -Wextra -Werror`, only standard headers plus `common/result.h`, and headers-only helpers from `common/`.
- Treat input as hostile: check bounds on every read, cap dimensions and allocations, and return `CODEC_TRUNCATED` or `CODEC_INVALID` instead of guessing.
- Return `enum codec_result`. `dt_error()` maps it to an AROS error.
- Test edge cases and malformed input in `tests/<name>.c`, not only the happy path.

## Shared code (`common/`)

| File | For | Provides |
|---|---|---|
| `result.h` | codecs | `enum codec_result` |
| `dtfile.[ch]` | any file-based datatype | `dt_new`, `dt_read_file`, `dt_error`, `dt_set_name`, `dt_write` |
| `dtpicture.[ch]` | picture datatypes | `dt_put_rgba`, `dt_picture_size`, `dt_each_row` |
| `zlib.[ch]` | codecs | `zlib_inflate`, `zlib_deflate`, `zlib_deflate_bound`: one-shot zlib streams through `z1.library`, or the system zlib in host tests |

- It's a library, not a framework. Use what fits and write format-specific code where it doesn't. A streaming or non-picture format shouldn't bend to fit these helpers.
- `build.sh` links `common/X.c` only when a file in the format includes `"common/X.h"`. It does not follow includes transitively, so include every common header you use directly.
- Add functions; never change the behaviour or signature of an existing one, because other formats' released binaries depend on it. If something doesn't fit, add a new function next to it.
- A change to `common/` goes in its own commit, before the format work that needs it. CI builds every format on every push, so breakage shows up there.
- Only add to `common/` once a second format needs it. The first user keeps it local.
- Anything that includes AROS headers is `.c` + `.h`. Pure helpers for codecs are `static inline` in a header, so host tests need no extra sources.

## Class conventions

- `OM_NEW` is `return dt_new(cl, obj, msg, load_<name>);`.
- `load_*` returns 0 or an AROS error. A `DTST_RAM` source (no file) is not an error.
- `DTM_WRITE`: pass non-`DTWM_RAW` modes to the superclass, and return TRUE for a NULL file handle, because MultiView probes support that way.
- Declare the superclass base with `ADD2LIBS`, matching `superclass` in the `.conf`.

## Build, test, release

```sh
make test                                     # host codec tests, all formats
make test-<name>                              # one format
make build TARGET=x86_64-aros PACKAGE=<name>  # needs the AROS toolbox; CI has it
make release PACKAGE=<name> LEVEL=minor       # bumps version and date, tags, pushes
```

- New formats start at `version 1.0`.
- The `date` in the `.conf` is the release date in the `$VER` string. `release.sh` updates it. Don't edit it by hand, and don't remove it: genmodule would then use the build time and builds would stop being reproducible.
- Pushing a `<name>-<major>.<minor>` tag builds and releases only that format.
- The AROS cross-build only runs in CI (`build.yml`: push to master, PRs, or a manual dispatch). A branch push alone does not build.
- Update the format's section in `README.md`: what it reads, what it saves, and where its descriptor comes from.
