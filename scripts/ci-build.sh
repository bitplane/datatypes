#!/bin/sh
set -eu

target=${1:?usage: ci-build.sh <target>}
case "$target" in
    i386-aros|aarch64-aros|x86_64-aros) ;;
    *) echo "Unknown target: $target" >&2; exit 1 ;;
esac

mkdir -p build
aros-cc -std=gnu11 -Wall -Wextra -Werror -c ci/probe.c -o "build/sdk-probe-$target.o"
