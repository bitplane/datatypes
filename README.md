# AROS image datatypes

Independent image loaders for AROS, distributed through the bitplane aros-pkg channel. The first planned classes are Targa and PCX, followed by QOI. Each class will be installable separately.

There are no datatype releases yet. The initial CI checks that the three Mountin AROS compiler images can compile code against the datatype SDK. Building and packaging loadable classes comes with the first decoder; a passing compiler check does not claim a working datatype.

## Versioning

Each datatype has its own version. Its module version, aros-pkg package version and release tag use the same two-number value: for example, `targa-45.1` releases `targa-datatype` version `45.1` on each supported AROS architecture. We will settle the initial module version when the first class is implemented. Bump and tag only the datatype being released; a change to shared code requires a deliberate bump for each affected class. No tag should be made until CI builds a loadable class and it has been tried in an AROS guest.

## CI and publishing

Pushes and pull requests run compiler checks for i386, aarch64 and x86_64 AROS using the existing Mountin images. The first decoder will extend CI to build real modules. A tag naming one datatype and its version will package only that class, publish its architecture-specific archives in a GitHub release, and then sync that package to aros-pkg. Package sync will be retryable and will not invalidate the GitHub release if the package service is unavailable. The `AROS_PKG_KEY` GitHub Actions secret is reserved for that publishing job; no private key is stored in this repository.
