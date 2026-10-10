#!/bin/sh
# Builds the libFuzzer programs in build/fuzz (needs clang).
#   tests/fuzz/build.sh            build them
#   tests/fuzz/run.sh NAME [SECS]  run one: image, text or config
set -eu
cd "$(dirname "$0")/../.."
OUT=build/fuzz
mkdir -p $OUT
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
INC="-Ithird_party/pdfium/include -isystem third_party/stb $(pkg-config --cflags sdl3 freetype2 fontconfig)"
CFLAGS="-std=c11 -D_GNU_SOURCE -O1 -g -fno-omit-frame-pointer $SAN $INC"
for f in src/*.c; do
    b=$(basename $f .c)
    extra=""
    [ "$b" = main ] && extra="-Dmain=npdf_main" # libFuzzer brings its own main
    [ $f -ot $OUT/$b.o ] 2>/dev/null || clang $CFLAGS $extra -fsanitize=fuzzer-no-link -c -o $OUT/$b.o $f
done
LIBS="-Lthird_party/pdfium/lib -lpdfium $(pkg-config --libs sdl3 freetype2 fontconfig) -lz -lm -Wl,-rpath,$PWD/third_party/pdfium/lib"
link() { # NAME and the objects it replaces
    name=$1; shift
    objs=""
    for o in $OUT/*.o; do
        skip=0
        for r in "$@"; do [ "$o" = "$OUT/$r.o" ] && skip=1; done
        [ $skip = 0 ] && objs="$objs $o"
    done
    clang $CFLAGS -fsanitize=fuzzer -o $OUT/fuzz_$name tests/fuzz/fuzz_$name.c $objs $LIBS
}
link image image
link text cite textbox
link config config
ls -l $OUT/fuzz_*
