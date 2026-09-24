# AROS image datatypes

Independent image loaders for AROS, distributed through the bitplane aros-pkg channel. The first planned classes are Targa and PCX, followed by QOI. Each class will be installable separately.

There are no datatype releases yet. The initial CI checks that the three Mountin AROS compiler images can compile code against the datatype SDK. Building and packaging loadable classes comes with the first decoder; a passing compiler check does not claim a working datatype.

## Versioning

Repository releases will use SemVer tags such as `v0.1.0`. Every package built from a release will carry that release version. AROS module `$VER` strings have only a version and revision, so each class will carry its own two-number module version and release date rather than squeezing a SemVer patch number into the revision. We will settle the initial module version when the first class is implemented. No tag should be made until CI builds a loadable class and it has been tried in an AROS guest.

## CI and publishing

Pushes and pull requests run compiler checks for i386, aarch64 and x86_64 AROS using the existing Mountin images. The first decoder will extend this workflow to build a real module, package its `.datatype` and descriptor as a separate installable archive per architecture, publish the archives in a GitHub release, and then sync them to aros-pkg. Package sync will be retryable and will not invalidate the GitHub release if the package service is unavailable. The `AROS_PKG_KEY` GitHub Actions secret is reserved for that publishing job; no private key is stored in this repository.
