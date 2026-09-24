#!/bin/sh
set -eu

tag=${1:?usage: publish.sh <tag> <channel-dir> <archive>...}
channel=${2:?usage: publish.sh <tag> <channel-dir> <archive>...}
shift 2
[ "$#" -eq 4 ] || { echo 'Expected four architecture archives' >&2; exit 2; }
sh scripts/tag-package.sh "$tag" > /dev/null
package=${tag%-*}
version=${tag##*-}
: "${PKG_SIGNKEY:?Set PKG_SIGNKEY to a publisher key file}"
pkg=${PKG:-pkg}
repo=${GITHUB_REPOSITORY:-bitplane/datatypes}
work_root=${RUNNER_TEMP:-${TMPDIR:-${HOME}/tmp}}
mkdir -p "$work_root"
work=$(mktemp -d "$work_root/datatype-publish.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

seen=' '
for archive in "$@"; do
    name=$(basename "$archive")
    case "$name" in
        "$package-$version-i386-aros.tar.bz2") arch=i386 ;;
        "$package-$version-aarch64-aros.tar.bz2") arch=aarch64 ;;
        "$package-$version-x86_64-aros.tar.bz2") arch=x86_64 ;;
        "$package-$version-m68k-aros.tar.bz2") arch=m68k ;;
        *) echo "Unexpected archive: $name" >&2; exit 2 ;;
    esac
    case "$seen" in *" $arch "*) echo "Duplicate architecture: $arch" >&2; exit 2 ;; esac
    seen="$seen$arch "
    archive=$(CDPATH='' cd "$(dirname "$archive")" && pwd)/$name
    source="$archive!/$package"
    "$pkg" MANIFEST "$source" KIND class NAME "$package-datatype" \
        VERSION "$version" > "$work/manifest"
    for field in "Name: $package-datatype" "Version: $version" \
                 "Architecture: $arch" "Kind: class"; do
        grep -Fxq "$field" "$work/manifest" || {
            echo "Unexpected manifest: expected $field" >&2; exit 1;
        }
    done
    "$pkg" PUBLISH "$source" CHANNEL "$channel" KIND class \
        NAME "$package-datatype" VERSION "$version" \
        UPSTREAM "https://github.com/$repo/releases/download/$tag/$name" \
        SHORT 'Targa image datatype for AROS' CATEGORY util/graphics \
        TAGS 'datatype, image, targa' AUTHOR bitplane \
        HOMEPAGE "https://github.com/$repo" REPOSITORY "https://github.com/$repo" \
        LICENSE MIT DISTRIBUTION open-source
done
for arch in i386 aarch64 x86_64 m68k; do
    case "$seen" in *" $arch "*) ;; *) echo "Missing architecture: $arch" >&2; exit 2 ;; esac
done
