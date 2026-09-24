#!/bin/sh
set -eu

package=${1:-targa}
level=${2:-minor}
dry_run=${3:-0}
[ -f "formats/$package/$package.conf" ] || { echo "Unknown datatype: $package" >&2; exit 2; }
case "$level" in current|minor|major) ;; *) exit 2 ;; esac
case "$dry_run" in 0|1) ;; *) exit 2 ;; esac
config="formats/$package/$package.conf"
current=$(sed -n 's/^version \([0-9][0-9]*\.[0-9][0-9]*\)$/\1/p' "$config")
[ -n "$current" ] || { echo "Invalid version in $config" >&2; exit 2; }
major=${current%.*}
minor=${current#*.}
case "$level" in
    current) next=$current ;;
    minor) next=$major.$((minor + 1)) ;;
    major) next=$((major + 1)).0 ;;
esac
tag="$package-$next"
[ "$(git branch --show-current)" = master ] || {
    echo 'Release from master' >&2; exit 1;
}
[ -z "$(git status --porcelain)" ] || {
    echo 'Working tree must be clean' >&2; exit 1;
}
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    echo "Tag already exists: $tag" >&2; exit 1
fi
printf 'Release %s from %s (%s)\n' "$tag" "$(git rev-parse --short HEAD)" "$level"
if [ "$dry_run" = 1 ]; then
    echo 'Dry run: no version change, tag, or push'
    exit 0
fi
if [ "$next" != "$current" ]; then
    sed -e "s/^version $current$/version $next/" \
        -e "s/^date .*/date $(date -u +%d.%m.%Y)/" "$config" > "$config.tmp"
    mv "$config.tmp" "$config"
    git add "$config"
    git commit -m "Release $package $next"
fi
sh scripts/tag-package.sh "$tag" > /dev/null
git tag -a "$tag" -m "$tag"
git push origin master "$tag"
