#!/bin/sh
set -eu

tag=${1:?usage: tag-package.sh <datatype-version>}
case "$tag" in *-*) package=${tag%-*}; version=${tag##*-} ;; *) exit 2 ;; esac
case "$package" in ''|[!a-z]*|*[!a-z0-9]*) exit 2 ;; esac
printf '%s\n' "$version" | grep -Eq '^[0-9]+\.[0-9]+$' || exit 2
config="formats/$package/$package.conf"
[ -f "$config" ] || exit 2
grep -Fxq "version $version" "$config" || {
    echo "Tag $tag differs from $config" >&2
    exit 2
}
printf '%s %s\n' "$package" "$version"
