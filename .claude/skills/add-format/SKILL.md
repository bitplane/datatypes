---
name: add-format
description: Add one new image format to this repository as an AROS datatype, from claiming it through to a ready pull request with a green four-target CI build. Use when asked to add, implement or pick up a format, or to take the next one from the queue.
argument-hint: "[format name, or blank for the next unclaimed format in queue.md]"
---

# Add a format

You are one of several sessions each adding a format in its own worktree. Nobody will repeat the context below, so follow it. `CLAUDE.md` holds the repository's layout and coding rules; read it first and treat it as part of these instructions.

The goal of the project is loader parity with ImageMagick, Pillow and netpbm on AROS. Every format you add is judged on real files from the wild, not only files it wrote itself.

## 1. Claim

1. Choose the format: the argument if one was given, otherwise the first entry in `queue.md` that is neither done nor claimed and not marked blocked.
2. Check it isn't taken. It is taken if `formats/<name>/` exists on `origin/master`, or a branch or PR mentions it: `git fetch origin`, `git branch -r`, `gh pr list --state all --search <name>`. Branch names may differ from the directory (`worktree-sgi-rgb` built `formats/sgi`), so read titles too. If it's taken, pick the next entry.
3. Check AROS doesn't already ship a class for it (see the AROS source under `$AROS_SOURCE`, `/opt/mountin/source` in the toolbox, `workbench/classes/datatypes/`). Never reuse a stock class's basename. An extension of a stock format gets a new basename, like `pam` next to `pnm`.
4. Work in a worktree on a branch named `worktree-<name>`. If you aren't in one, create it (the `EnterWorktree` tool, or `claude --worktree <name>` for a person). Push the branch straight away with `git push -u origin HEAD` so other sessions see the claim.
5. If the entry is marked blocked in `queue.md`, or the work turns out to need a change outside the format's own files, stop and report what's needed. Don't make shared changes in a format branch.

## 2. Research

- Read the format's specification and note every variant: depths, compression, colour models, byte order, optional blocks, multi-image structure.
- Find out which variants ImageMagick, Pillow and netpbm read and write. All three are installed locally (`magick`, the netpbm tools, `python3 -c 'import PIL'`).
- Real-world files are welcome for testing: download them, or make them with the reference tools. Keep them outside the repository or under `build/`. Never commit files you didn't create.
- You may read other implementations for understanding. Don't translate or port their code; write your own.

## 3. Scope

- **Reading:** support every variant that at least two of the reference tools read. Add variants that only one reads where practical. Reject the rest with `CODEC_INVALID`, and list them as unsupported in the format's README section.
- **Writing:** implement `DTM_WRITE` wherever the format has a sensible writer, choosing the simplest lossless variant that covers the image (for example opaque vs alpha, like Targa's 24/32-bit choice). Read-only is fine where writing makes no sense.
- **Multi-image files** (pages, frames, mipmaps, icon directories, layers): support `PDTA_WhichPicture` (the index to load, in file order) and `PDTA_GetNumPictures` (a `ULONG *` filled in with the count), both given in `OM_NEW`'s tags. `dt_new` doesn't pass tags to the loader, so write your own `OM_NEW` that reads them from `msg->ops_AttrList`. When no index is requested, load the first image for pages, frames and mipmaps (as AROS's stock classes do), and the largest, then deepest, image for icon formats.

## 4. Codec policy

- **Memory safety is strict; content handling is forgiving.** Bounds-check every read and cap every allocation, but decode what real files contain:
  - accept structures the spec allows even when they're unusual, such as optional blocks that are present but unused
  - ignore optional metadata that is malformed, instead of rejecting the file
  - clamp an RLE run that overruns the last pixel
- **Truncated files** are an error: `CODEC_TRUNCATED`. Don't show partial images.
- **Limits:** at most 16M pixels, and each side at most 65535 (the bitmap header is 16-bit).
- **Alpha:**
  - Convert premultiplied alpha to straight alpha.
  - If every pixel's alpha is zero, show the image opaque. Many writers declare alpha and leave it empty.
  - Only trust alpha that the header says is there, such as attribute bits or a flag. Don't infer it from the storage depth.
  - When saving to a variant without alpha, composite over white.
- **High bit depth:** round 16-bit channels to 8-bit. For float data, clamp to 0–1, then apply the sRGB transfer curve.
- **Compression and other libraries:** use AROS system libraries through their standard C API. For zlib that means `z1.library` via a `common/zlib.h` wrapper. If the wrapper doesn't exist yet, the format is blocked: stop and report. Never vendor third-party code.
- **Portability:** nothing OS-specific in codecs. Only the class glue and `common/dt*` may touch AROS, so the codecs could later serve other Amiga-family systems.

## 5. Descriptor

- If AROS ships a descriptor for the format (`workbench/devs/datatypes/` in the AROS source), use it and don't ship one.
- Otherwise ship `<NAME>.dtd` and the compiled `<NAME>.dtyp` (from AROS's `createdtdesc`). Match on content: magic bytes via the mask, with a filename pattern as a secondary check.
- For formats without magic, follow WBMP: match what the header must contain, require the filename pattern, and use priority -10. `FindDtInList` checks the pattern after the mask, so this doesn't claim other files.

## 6. Verify

- Host tests in `tests/<name>.c`, run with `make test-<name>` under ASan and UBSan. Cover every supported variant, both orientations or byte orders where they apply, multi-image selection, and malformed input: truncation at each structure boundary, overflowing sizes, bad indexes and reserved values.
- Before opening the PR, run the decoder over mutated inputs for a while under ASan and UBSan. A throwaway harness under `build/` is fine; don't commit it.
- Cross-check against the reference tools. Your decoded RGBA must match at least one of them pixel for pixel on real and generated files covering each variant, and files your writer produces must decode identically in them. Where the tools disagree with each other or with the spec, say which you followed and why.
- `make test` for every format must still pass.
- Do nothing beyond this about testing inside AROS. That is done separately, by others, on purpose.

## 7. Pull request

1. Commit in the style of the existing history. Update the format's section in `README.md`: what it reads, what it saves, and where its descriptor comes from.
2. Open a draft PR against `master`. The body covers:
   - what loads and saves, and which variants are unsupported
   - how the descriptor recognises files
   - the tests you ran and what the cross-checks showed, including any disagreements with the reference tools
   - anything you left out, and anything a later format could learn from, under **Lessons**
3. Watch CI (`gh run watch`) and fix failures until all four AROS targets build. The cross-build only runs in CI.
4. When CI is green, mark the PR ready (`gh pr ready`). That is the hand-off. Releasing and tagging are not part of this work.

Don't edit this skill or `queue.md` from a format branch. Put suggestions in the PR's **Lessons** section instead.

## Lessons

Rules learnt from earlier formats. They apply to every new one.

- **Targa:** writers often declare alpha (attribute bits, 5:5:5:1 top bits, 32-bit palettes) and leave it zero, so an all-zero alpha channel means opaque. Palette alpha counts only when the pixel descriptor declares attribute bits.
- **Targa:** true-colour files may carry an unused colour map that the spec allows. Skip it rather than rejecting the file.
- **Targa:** an extension area or footer that points to garbage should be ignored, not treated as fatal.
- **WBMP:** a format with no magic still needs a descriptor that won't claim unrelated files: a header mask, the filename pattern and a low priority.
