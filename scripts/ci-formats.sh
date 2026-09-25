#!/usr/bin/env bash
set -euo pipefail

if (( $# != 3 )); then
    echo 'usage: ci-formats.sh <base-commit> <head-commit> <event-name>' >&2
    exit 2
fi

all_formats() {
    find formats -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort
}

# A first push or a missing comparison commit cannot safely narrow the build.
if ! git cat-file -e "$1^{commit}" 2>/dev/null ||
   ! git cat-file -e "$2^{commit}" 2>/dev/null; then
    all_formats
    exit 0
fi

base=$1
if [[ $3 == pull_request ]]; then
    # Compare the PR's own changes, excluding commits added to master since it branched.
    base=$(git merge-base "$1" "$2") || { all_formats; exit 0; }
fi

selected=()
while IFS= read -r -d '' path; do
    case "$path" in
        formats/*/README.md)
            ;;
        formats/*/*)
            format=${path#formats/}
            selected+=("${format%%/*}")
            ;;
        common/zlib.c|common/zlib.h)
            # Only formats that use the zlib wrapper need an SDK rebuild.
            for source in formats/*/*.[ch]; do
                if grep -Fq '"common/zlib.h"' "$source"; then
                    format=${source#formats/}
                    selected+=("${format%%/*}")
                fi
            done
            ;;
        common/ico.c|common/ico.h|common/icoenc.c|common/icoenc.h|common/dtembed.c|common/dtembed.h)
            # These helpers are used by ANI and ICO. Keep their SDK checks together.
            selected+=(ani ico)
            ;;
        Makefile|CLAUDE.md|scripts/ci-formats.sh|scripts/aros/package.sh)
            # The decoder job covers host build changes; these do not alter SDK builds.
            ;;
        tests/aros-check.c)
            all_formats
            exit 0
            ;;
        tests/*.c)
            format=${path#tests/}
            selected+=("${format%.c}")
            ;;
        README.md|LICENSE*|.gitignore|docs/*|.claude/skills/*)
            ;;
        *)
            # Shared code, build scripts, or an unclassified file may affect all formats.
            all_formats
            exit 0
            ;;
    esac
done < <(git diff --name-only -z "$base" "$2" --)

for format in "${selected[@]}"; do
    if [[ -d "formats/$format" ]]; then
        printf '%s\n' "$format"
    fi
done | sort -u
