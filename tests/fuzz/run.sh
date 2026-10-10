#!/bin/sh
# tests/fuzz/run.sh NAME [SECONDS]   NAME: image, text or config (build with tests/fuzz/build.sh first)
# FUZZ_JOBS=4 runs that many workers that share the corpus.
# Inputs that crash it are kept in build/fuzz/crashes-NAME; the interesting ones grow build/fuzz/corpus-NAME.
set -eu
cd "$(dirname "$0")/../.."
name=${1:?image, text or config}; secs=${2:-300}
bin=build/fuzz/fuzz_$name; corpus=build/fuzz/corpus-$name; crashes=build/fuzz/crashes-$name
mkdir -p $corpus $crashes
case $name in
image) # 4 bytes of size, then a real image
    for f in tests/qa/corpus/*.png tests/qa/corpus/*.jpg tests/qa/corpus/*.bmp icons/*.png; do
        [ -f "$f" ] && { printf '\040\040\000\000' ; cat "$f"; } > $corpus/seed-$(basename $f); done 2>/dev/null || true ;;
text)
    printf '\000Hello world, a line of text to wrap in a box' > $corpus/s0
    printf '\001 12 200 10 700\nsome text\nmore' > $corpus/s1
    printf '\002see doi:10.1103/PhysRevLett.116.061102 and arXiv:2101.01234v2' > $corpus/s2
    printf '\003a b<c>#d' > $corpus/s3
    printf '\0043-7,9' > $corpus/s4 ;;
config)
    cp config.example $corpus/s0 2>/dev/null || true
    for f in themes/*.conf; do [ -f "$f" ] && cp "$f" $corpus/$(basename $f); done ;;
esac
ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 exec $bin -max_total_time=$secs -jobs=${FUZZ_JOBS:-1} -workers=${FUZZ_JOBS:-1} -timeout=20 -rss_limit_mb=3000 \
    -print_final_stats=1 -artifact_prefix=$crashes/ $corpus
