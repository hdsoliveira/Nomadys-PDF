#!/bin/sh
# Stress test with sanitizers: malformed PDFs, then random key sequences.
#   tests/qa/run.sh [WORKDIR] [SEEDS] [KEYS]     (defaults: /tmp/nomadys-qa 8 1500)
set -e
cd "$(dirname "$0")/../.."
work=${1:-/tmp/nomadys-qa}; seeds=${2:-8}; keys=${3:-1500}
make clean >/dev/null
make CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" >/dev/null
mv npdf npdf-asan && make clean >/dev/null && make >/dev/null
python3 tests/qa/make_corpus.py "$work/corpus" >/dev/null
mkdir -p "$work/cfg/nomadys-pdf" # \f looks only at the corpus, never at your own PDFs
echo "pdf_dirs = $work/corpus" > "$work/cfg/nomadys-pdf/config"
seq='<drag:150,200,600,260>C<drag:600,260,150,200>y<drag:50,50,50,50>vrok<Enter>r<C-u><Enter>u<C-r>rmore<Enter><Esc>:response<Enter>:response! r.md<Enter>jjkkGgg+++---=zdir\l<Tab><Tab><Tab>jk<Enter>v<Esc>vjwv$C<Esc><C-v>jjlllvVx<Esc><Esc><C-v>vjjjjllllyvJJJJJJJJLLLLLLLLLLLLLLLLLYy<Esc>iY<C-v>vlly<Esc>i/a<Enter>nN5n<C-j>abc<BS><Esc>:rotate 90<Enter>:zoom 400<Enter>jjll:zoom 5<Enter><resize:1,1>jj<resize:3000,2000>\maGma<C-o><C-i>mm\cljjG/ma<Enter>j<Esc>4k<Esc>fa<Esc>Fa<Enter>f<Esc>vjjc$x^2$<S-Enter>$$\frac{a}{b}$$ $\bad$<wait:2000><Left><Up><Enter>:image '"$work"'/corpus/pic.png<Enter>lljj+-HLJK=rrRchjklHJKLjjjj<C-h>u<Esc>chhkk<Enter>r=<Enter><C-v>vjjjjllllllBe<C-l>JJ<Esc>ex<Esc>:image /nonexistent.png<Enter>/sta<BS>e<C-t>x<Esc>/a<Enter>nN\oab<C-j><C-k><BS><Enter>\ollj+-G<Enter>\g<Esc>:pages<Enter>q:session<Enter>:recent<Enter><Esc><F5>jjk.b.12G<BS>gg<Esc>:diff '"$work"'/corpus/text.pdf<Enter>]c]c[c[c:nodiff<Enter>]c\Tjjijkk<Enter>\Tj<Esc>:theme catppuccin<Enter>:theme zzz<Enter>:theme new qa<Enter>:theme new qa<Enter>:theme new ../x<Enter>:theme<Enter>q\ax:about<Enter><Esc>d<C-n>q:reload<Enter><wait:300><C-v>vjjllCu<C-r>uu<C-r><C-r>:merge '"$work"'/corpus/text.pdf<Enter>:merge '"$work"'/in.pdf<Enter>u<C-r>u<C-w>vjjJ<C-w>w+<C-w>s<C-w>x/a<Enter>n<C-w>q<C-w>o:vsplit '"$work"'/corpus/text.pdf<Enter>G<C-w>hi<C-w>l<C-w><C-w>:close<Enter>:split<Enter>:only<Enter>:only<Enter>vwwY<Esc>vY<Esc>\fsa<C-j><Enter>\f<Esc>:find<Enter><Esc>\/li<wait:1500>ne<C-j><C-k><C-j><Enter><wait:200>\n<Tab>a<BS><Tab><C-u><Esc>:library zebra alpha<Enter><wait:800><Enter>:comments<Enter><wait:300><C-j><Enter><wait:300>\g<wait:200>vlr<lt>>xu<C-r>uRyx<Enter>i'"$work"'/corpus/text.pdf<Enter>uG<lt><lt>vggx<Esc>:extract 1-2<Enter>:extract 9-3 x.pdf<Enter>:delete 2<Enter>u:append '"$work"'/corpus/text.pdf<Enter>:insert /nonexistent.pdf<Enter>u<C-r>uuu<wait:300>'
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
