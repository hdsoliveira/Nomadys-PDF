#!/bin/sh
# Stress test with sanitizers: malformed PDFs, then random key sequences.
#   tests/qa/run.sh [WORKDIR] [SEEDS] [KEYS]     (defaults: /tmp/nomadys-qa 8 1500)
# Runs QA_JOBS at once (default 4), each in WORKDIR/jobs/NAME with its own
# copy of the PDF, config, data and cache; logs in WORKDIR/jobs/NAME/logs.
set -e
cd "$(dirname "$0")/../.."
work=${1:-/tmp/nomadys-qa}; seeds=${2:-8}; keys=${3:-1500}; jobs=${QA_JOBS:-4}
make clean >/dev/null
make -j"$jobs" CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" >/dev/null
mv npdf npdf-asan && make clean >/dev/null && make -j"$jobs" >/dev/null
python3 tests/qa/make_corpus.py "$work/corpus" >/dev/null
mkdir -p "$work/cfg/nomadys-pdf" # \f looks only at the corpus, never at your own PDFs
echo "pdf_dirs = $work/corpus" > "$work/cfg/nomadys-pdf/config"
seq='<drag:150,200,600,260>C<drag:600,260,150,200>y<drag:50,50,50,50>vrok<Enter>r<C-u><Enter>u<C-r>rmore<Enter><Esc>:response<Enter>:response! r.md<Enter>jjkkGgg+++---=zdir\l<Tab><Tab><Tab>jk<Enter>v<Esc>vjwv$C<Esc><C-v>jjlllvVx<Esc><Esc><C-v>vjjjjllllyvJJJJJJJJLLLLLLLLLLLLLLLLLYy<Esc>iY<C-v>vlly<Esc>i/a<Enter>nN5n<C-j>abc<BS><Esc>:rotate 90<Enter>:zoom 400<Enter>jjll:zoom 5<Enter><resize:1,1>jj<resize:3000,2000>\maGma<C-o><C-i>mm\cljjG/ma<Enter>j<Esc>4k<Esc>fa<Esc>Fa<Enter>f<Esc>vjjc$x^2$<S-Enter>$$\frac{a}{b}$$ $\bad$<wait:2000><Left><Up><Enter>:image '"$work"'/corpus/pic.png<Enter>lljj+-HLJK=rrRchjklHJKLjjjj<C-h>u<Esc>chhkk<Enter>r=<Enter><C-v>vjjjjllllllBe<C-l>JJ<Esc>ex<Esc>:image '"$work"'/corpus/pic.png<Enter>8+oLLl<Enter>fFr<down:470,150><move:500,260><move:520,300><up:520,300><down:541,100><move:600,200><up:600,200>o<drag:500,300,560,360><Esc>u<C-r><Esc>:image '"$work"'/corpus/pic.png<Enter>20+<Enter><down:300,300><up:300,300><down:2,2><move:400,500><up:400,500><Esc><C-v>jjjvlllT<lt>b>free text é 日本<Enter>more words to wrap around<C-=><C-=><Up><Home><C-w><Esc>elllLLHH++--<down:500,200><up:500,200>i<BS><BS>x<Esc>u<C-r>xu:image /nonexistent.png<Enter>/sta<BS>e<C-t>x<Esc>/a<Enter>nN\oab<C-j><C-k><BS><Enter>\ollj+-G<Enter>\g<Esc>:pages<Enter>q:session<Enter>:recent<Enter><Esc><F5>jjk.b.12G<BS>gg<Esc>:diff '"$work"'/corpus/text.pdf<Enter>]c]c[c[c:nodiff<Enter>]c\Tjjijkk<Enter>\Tj<Esc>:theme catppuccin<Enter>:theme zzz<Enter>:theme new qa<Enter>:theme new qa<Enter>:theme new ../x<Enter>:theme<Enter>q\ax:about<Enter><Esc>d<C-n>q:reload<Enter><wait:300><C-v>vjjllCu<C-r>uu<C-r><C-r>:merge '"$work"'/corpus/text.pdf<Enter>:merge @IN@<Enter>u<C-r>u<C-w>vjjJ<C-w>w+<C-w>s<C-w>x/a<Enter>n<C-w>q<C-w>o:vsplit '"$work"'/corpus/text.pdf<Enter>G<C-w>hi<C-w>l<C-w><C-w>:close<Enter>:split<Enter>:only<Enter>:only<Enter>vwwY<Esc>vY<Esc>\fsa<C-j><Enter>\f<Esc>:find<Enter><Esc>\/li<wait:1500>ne<C-j><C-k><C-j><Enter><wait:200>\n<Tab>a<BS><Tab><C-u><Esc>:library zebra alpha<Enter><wait:800><Enter>:comments<Enter><wait:300><C-j><Enter><wait:300>\g<wait:200>vlr<lt>>xu<C-r>uRyx<Enter>i'"$work"'/corpus/text.pdf<Enter>uG<lt><lt>vggx<Esc>:extract 1-2<Enter>:extract 9-3 x.pdf<Enter>:delete 2<Enter>u:append '"$work"'/corpus/text.pdf<Enter>:insert /nonexistent.pdf<Enter>:abc def<Left><Left><Home><Del>X<End><C-w><C-a><C-e><Right><Esc>:form save qa<Enter>:form qa<Enter>:form nope<Enter>:form<Enter>nqa2<Enter>jx<C-u>y<Enter><Esc>\F<Down>x<Esc><Esc>:form delete qa<Enter><C-v><hover:220,220><down:200,200><move:260,260><up:260,260><Esc>v<hover:300,150><hover:5,5><drag:300,300,100,100>C<Esc>u<C-r>uuu<wait:300>'
WORK=$work SEQ=$seq SEEDS=$seeds KEYS=$keys JOBS=$jobs python3 - <<'EOF'
import concurrent.futures as cf, glob, os, shutil, subprocess, sys
work, seq, keys = os.environ['WORK'], os.environ['SEQ'], os.environ['KEYS']
seeds, jobs = int(os.environ['SEEDS']), int(os.environ['JOBS'])

def run(name, src, script, timeout):
    d = f'{work}/jobs/{name}'
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(f'{d}/cfg/nomadys-pdf')
    shutil.copy(f'{work}/cfg/nomadys-pdf/config', f'{d}/cfg/nomadys-pdf/config')
    target = src
    if os.path.exists(src): # never edit the originals
        target = f'{d}/in.pdf'
        shutil.copy(src, target)
    if script is None:
        script = subprocess.run(['python3', 'tests/qa/fuzz.py', name.split('-')[1], keys, work],
                                capture_output=True, text=True, check=True).stdout.strip()
    script = script.replace('@IN@', target)
    p = subprocess.run(['python3', 'tests/qa/check.py', d, name, str(timeout), '--', '--test-out', d,
                        '--test-script', script, target], capture_output=True, text=True)
    return p.stdout.strip(), p.returncode

runs = [(f'open-{os.path.basename(f)[:-4]}', f, seq, 900) # the fixed sequence takes ~2 min without sanitizers
        for f in sorted(glob.glob(f'{work}/corpus/*.pdf')) + ['tests/sample.pdf', '/nonexistent.pdf']]
runs += [(f'fuzz-{s}', 'tests/sample.pdf', None, 3000) for s in range(1, seeds + 1)]
fail = 0
with cf.ThreadPoolExecutor(jobs) as ex:
    for line, code in ex.map(lambda r: run(*r), runs): # in order, as each is known
        print(line, flush=True)
        fail |= code != 0
sys.exit(fail)
EOF
