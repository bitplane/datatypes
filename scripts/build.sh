#!/bin/sh
set -eu

target=${1:?usage: build.sh <target> [datatype]}
datatype=${2:-targa}
case "$target" in i386-aros|aarch64-aros|x86_64-aros|m68k-aros) ;; *) exit 2 ;; esac
[ -f "formats/$datatype/$datatype.conf" ] || { echo "Unknown datatype: $datatype" >&2; exit 2; }
: "${AROS_SOURCE:=/opt/mountin/source}"
: "${AROS_CC:=aros-cc}"

work="build/$target/$datatype"
output="dist/$target"
mkdir -p "$work" "$output"

# The m68k SDK declares datatype method arguments as volatile registers.
# GCC warns about those ABI declarations, which are outside this class's control.
if [ "$target" = m68k-aros ]; then
    set -- -Wno-volatile-register-var
else
    set --
fi

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
"$AROS_CC" -std=gnu11 -Wall -Wextra -Werror "$@" -c \
    "formats/$datatype/${datatype}class.c" -o "$work/class.o"
objects=
for source in "formats/$datatype"/*.c; do
    case "$source" in *class.c) continue ;; esac
    object="$work/$(basename "$source" .c).o"
    "$AROS_CC" -std=c99 -Wall -Wextra -Werror -c "$source" -o "$object"
    objects="$objects $object"
done
"$AROS_CC" -I"$work" -c "$work/${datatype}_start.c" -o "$work/start.o"
"$AROS_CC" -I"$work" -c "$work/${datatype}_end.c" -o "$work/end.o"
"$AROS_CC" -nostartfiles "$work/start.o" "$work/class.o" $objects \
    "$work/end.o" -lstdc_rel -o "$output/$datatype.datatype"
