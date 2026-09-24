#!/bin/sh
set -eu

target=${1:?usage: build.sh <target> [datatype]}
datatype=${2:-targa}
case "$target" in i386-aros|aarch64-aros|x86_64-aros) ;; *) exit 2 ;; esac
case "$datatype" in targa) ;; *) echo "Unknown datatype: $datatype" >&2; exit 2 ;; esac
: "${AROS_SOURCE:=/opt/mountin/source}"
: "${AROS_CC:=aros-cc}"

work="build/$target/$datatype"
output="dist/$target"
mkdir -p "$work" "$output"

if [ -n "${GENMODULE:-}" ]; then
    generator=$GENMODULE
else
    generator="$work/genmodule"
    make -s -C "$AROS_SOURCE/tools/genmodule" \
        GENMODULE="$(pwd)/$generator" \
        SRCDIR="$AROS_SOURCE" TOP="$AROS_SOURCE" \
        CURDIR=tools/genmodule GENINCDIR="$AROS_SOURCE/tools/genmodule" \
        HOST_CFLAGS=-O2
fi

config="formats/$datatype/$datatype.conf"
"$generator" -c "$config" -d "$work" writelibdefs "$datatype" datatype
"$generator" -c "$config" -d "$work" writefiles "$datatype" datatype
"$AROS_CC" -std=gnu11 -Wall -Wextra -Werror -c \
    "formats/$datatype/${datatype}class.c" -o "$work/class.o"
"$AROS_CC" -std=c99 -Wall -Wextra -Werror -c \
    "formats/$datatype/decode.c" -o "$work/decode.o"
"$AROS_CC" -I"$work" -c "$work/${datatype}_start.c" -o "$work/start.o"
"$AROS_CC" -I"$work" -c "$work/${datatype}_end.c" -o "$work/end.o"
"$AROS_CC" -nostartfiles "$work/start.o" "$work/class.o" \
    "$work/decode.o" "$work/end.o" -lstdc_rel -o "$output/$datatype.datatype"
