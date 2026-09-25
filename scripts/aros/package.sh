#!/bin/sh
set -eu

target=${1:?usage: package.sh <target> <datatype> <version>}
package=${2:?usage: package.sh <target> <datatype> <version>}
version=${3:?usage: package.sh <target> <datatype> <version>}
case "$target" in i386-aros|aarch64-aros|x86_64-aros|m68k-aros) ;; *) exit 2 ;; esac
sh scripts/tag-package.sh "$package-$version" > /dev/null

module="dist/$target/$package.datatype"
[ -f "$module" ] || { echo "Missing $module" >&2; exit 1; }
name="$package-$version-$target"
work_root=${RUNNER_TEMP:-${TMPDIR:-${HOME}/tmp}}
mkdir -p "$work_root" dist
work=$(mktemp -d "$work_root/datatype-package.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/$package/Classes/DataTypes" "$work/$package/Help/$package"
cp "$module" "$work/$package/Classes/DataTypes/"
for source in formats/$package/*.dtyp; do
    [ -f "$source" ] || continue
    descriptor=${source##*/}
    descriptor=${descriptor%.dtyp}
    mkdir -p "$work/$package/Devs/DataTypes"
    cp "$source" "$work/$package/Devs/DataTypes/$descriptor"
    chmod 644 "$work/$package/Devs/DataTypes/$descriptor"
done
cp LICENSE "$work/$package/Help/$package/LICENSE"
chmod 644 "$work/$package/Classes/DataTypes/$package.datatype" \
          "$work/$package/Help/$package/LICENSE"
epoch=${SOURCE_DATE_EPOCH:-$(git show -s --format=%ct HEAD)}
tar -C "$work" --sort=name --mtime="@$epoch" --owner=0 --group=0 \
    --numeric-owner -cjf "dist/$name.tar.bz2" "$package"
(cd dist && sha256sum "$name.tar.bz2") > "dist/$name.tar.bz2.sha256"
