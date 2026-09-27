#!/bin/sh
# Stress test with sanitizers: malformed PDFs, then random key sequences.
#   tests/qa/run.sh [WORKDIR] [SEEDS] [KEYS]     (defaults: /tmp/nomadys-qa 8 1500)
set -e
cd "$(dirname "$0")/../.."
work=${1:-/tmp/nomadys-qa}; seeds=${2:-8}; keys=${3:-1500}
make clean >/dev/null
make CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" >/dev/null
mv nomadys-pdf nomadys-pdf-asan && make clean >/dev/null && make >/dev/null
python3 tests/qa/make_corpus.py "$work/corpus" >/dev/null
seq='jjkkGgg+++---=zdir\l<Tab><Tab><Tab>jk<Enter>v<Esc>vjwv$C<Esc><C-v>jjlllvVx<Esc><Esc>/a<Enter>nN5n<C-j>abc<BS><Esc>:rotate 90<Enter>:zoom 400<Enter>jjll:zoom 5<Enter><resize:1,1>jj<resize:3000,2000>\maGma<C-o><C-i>mm\cljjG/ma<Enter>j<Esc>4k<Esc>fa<Esc>Fa<Enter>f<Esc>vjjc$x^2$<S-Enter>$$\frac{a}{b}$$ $\bad$<wait:2000><Left><Up><Enter>:image '"$work"'/corpus/pic.png<Enter>lljj+-HLJK=rrRchjklHJKLjjjj<C-h>u<Esc>chhkk<Enter>r=<Enter><C-v>vjjjjllllllBe<C-l>JJ<Esc>ex<Esc>:image /nonexistent.png<Enter>d<C-n>q:reload<Enter><wait:300>'
fail=0
for f in "$work"/corpus/*.pdf tests/sample.pdf /nonexistent.pdf; do
    target=$f
    cp "$f" "$work/in.pdf" 2>/dev/null && target=$work/in.pdf # never edit the originals
    python3 tests/qa/check.py "$work" "open-$(basename "$f" .pdf)" 300 -- --test-out "$work" --test-script "$seq" "$target" || fail=1
done
for s in $(seq 1 "$seeds"); do
    cp tests/sample.pdf "$work/fuzz.pdf"
    python3 tests/qa/check.py "$work" "fuzz-$s" 3000 -- --test-out "$work" \
        --test-script "$(python3 tests/qa/fuzz.py "$s" "$keys" "$work")" "$work/fuzz.pdf" || fail=1
done
exit $fail
