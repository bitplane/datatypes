# AROS image datatypes

Independent image loaders for AROS, distributed through the bitplane aros-pkg channel. The first planned classes are Targa and PCX, followed by QOI. Each class will be installable separately.

There are no datatype releases yet. The Targa class can load colour-mapped, true-colour and grayscale TGA files (including RLE) and save uncompressed 32-bit TGA with alpha. It has been tested in the x86_64 AROS guest, including a save/reload round trip. CI builds the class for three AROS targets and keeps the modules as workflow artifacts; it does not publish a release yet.

## Versioning

Each datatype has its own version. Its module version, aros-pkg package version and release tag use the same two-number value: for example, `targa-45.1` releases `targa-datatype` version `45.1` on each supported AROS architecture. We will settle the initial module version when the first class is implemented. Bump and tag only the datatype being released; a change to shared code requires a deliberate bump for each affected class. No tag should be made until CI builds a loadable class and it has been tried in an AROS guest.

## CI and publishing

Pushes and pull requests build the class for i386, aarch64 and x86_64 AROS using the Mountin images and run the native decoder tests. A later release workflow will package only the tagged datatype, publish architecture-specific archives in a GitHub release, then sync to aros-pkg in a retryable job. The `AROS_PKG_KEY` GitHub Actions secret is reserved for that publishing job; no private key is stored in this repository.
