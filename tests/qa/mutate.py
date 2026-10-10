"""Fuzzing with malformed annotations: a PDF full of notes is spoiled field by field.

usage: mutate.py [WORKDIR] [COUNT] [FIRST_SEED]   (BIN=npdf-asan by default; JOBS=4)

A seed PDF is made with the program itself (highlights with comments, a task, an area,
a picture turned and mirrored, a free text). Each mutant has the numbers and strings
of its annotations replaced by nasty ones (huge, negative, NaN, empty, very long,
broken UTF-16, formats, unbalanced brackets) and a few random bytes changed. The
program then opens it and walks through everything that reads an annotation: the
panel and its tabs, search in the comments, the export and the response letter,
:diff, clicks that select pictures and texts and keys that change them, writing a
text, saving. Anything the sanitizers or a timeout catch is reported with the seed
number that makes it again (mutate.py WORKDIR 1 SEED).
"""
import concurrent.futures as cf, os, random, re, shutil, subprocess, sys

repo = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '/tmp/nomadys-mutate')
count = int(sys.argv[2]) if len(sys.argv) > 2 else 200
first = int(sys.argv[3]) if len(sys.argv) > 3 else 1
jobs = int(os.environ.get('JOBS', 4))
binary = os.environ.get('BIN', f'{repo}/npdf-asan')
plain = f'{repo}/npdf'
os.makedirs(work, exist_ok=True)
subprocess.run(['python3', f'{repo}/tests/qa/make_corpus.py', f'{work}/corpus'], check=True, stdout=subprocess.DEVNULL)
PIC = f'{work}/corpus/pic.png'

NUMBERS = ['0', '-1', '1', '1e30', '-1e30', '3.4e38', '1e-30', '0.0000001', '2147483648', '4294967296',
           '99999999999999999999', '-0', '1e308', '65536', '-65536', '32767.99', 'nan', 'inf', '-inf']
STRINGS = ['', 'A' * 5000, '\\376\\377\\000A\\330', '\\376\\377\\330\\000\\334', '%s%s%s%n', '\\(\\(\\(', '$$', '$\\input{x}$',
           '$$$$$$$$$$$$', '\\\\\\\\', '1e308 1e308', '-5 -5', 'nan nan', '0 0', '10 200', '12 0', '0 12', '1 1 1',
           '0 0 1 1 0 0 0 0 0 0 0 0 0 0', '1e9 1e9 1e9 1e9 0 0 1 1 1 1 1 1e9 1e9 1e9', 'open', 'done 2026-01-01',
           'done 9999999999-99-99', '\\000\\000', 'x' * 300, '\\r\\n\\r\\n\\r\\n', '\\377\\376A\\000']


def make_seed():
    """A PDF with every kind of note the program makes."""
    d = f'{work}/seed'
    shutil.rmtree(d, ignore_errors=True)
    for sub in ('cfg/nomadys-pdf', 'home', 'data', 'cache', 'run'):
        os.makedirs(f'{d}/{sub}')
    os.chmod(f'{d}/run', 0o700)
    open(f'{d}/cfg/nomadys-pdf/config', 'w').write(f'pdf_dirs = {d}\nautosave = false\nbackup = false\n')
    shutil.copy(f'{repo}/tests/sample.pdf', f'{d}/seed.pdf')
    script = ('vjjj0veeecfirst note<Enter><Esc><Esc>vjjj0veeeCt<Esc><Esc>vjjjjj0veeeVt<Esc><Esc>'
              f'<C-v>jjjjjjvlllllljjjjC<Esc>:image {PIC}<Enter>10+oLLf<Enter><Esc>'
              '<C-v>jjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjT' + 'a free text<Esc><Esc>:w<Enter><wait:300>q')
    env = dict(os.environ, HOME=f'{d}/home', XDG_DATA_HOME=f'{d}/data', XDG_CONFIG_HOME=f'{d}/cfg',
               XDG_CACHE_HOME=f'{d}/cache', XDG_RUNTIME_DIR=f'{d}/run', SDL_VIDEODRIVER='offscreen',
               SDL_RENDER_DRIVER='software')
    subprocess.run([plain, '--test-out', d, '--test-script', script, f'{d}/seed.pdf'], env=env, timeout=120,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return open(f'{d}/seed.pdf', 'rb').read()


def mutate(data, rng):
    out = data
    keys_num = [rb'/Rect\[', rb'/QuadPoints\[', rb'/BBox\[', rb'/Matrix\[', rb'/MediaBox\[', rb'/C\[', rb'/Border\[']
    keys_str = [rb'/Contents\(', rb'/NM\(', rb'/T\(', rb'/Subj\(', rb'/NomadysImage\(', rb'/NomadysText\(',
                rb'/NomadysTask\(', rb'/NomadysArea\(', rb'/State\(', rb'/StateModel\(', rb'/Name\(']
    for _ in range(rng.randint(1, 5)):
        kind = rng.random()
        if kind < 0.4:  # numbers in an array
            k = rng.choice(keys_num)
            spots = [m for m in re.finditer(k + rb'([^\]]*)\]', out)]
            if spots:
                m = rng.choice(spots)
                nums = m.group(1).split()
                if nums:
                    nums[rng.randrange(len(nums))] = rng.choice(NUMBERS).encode()
                    out = out[:m.start(1)] + b' ' + b' '.join(nums) + b' ' + out[m.end(1):]
        elif kind < 0.8:  # a string
            k = rng.choice(keys_str)
            spots = [m for m in re.finditer(k + rb'((?:[^()\\]|\\.)*)\)', out)]
            if spots:
                m = rng.choice(spots)
                out = out[:m.start(1)] + rng.choice(STRINGS).encode('latin-1') + out[m.end(1):]
        else:  # a few bytes
            for _ in range(rng.randint(1, 4)):
                i = rng.randrange(len(out))
                out = out[:i] + bytes([rng.randrange(256)]) + out[i + 1:]
    return out


def script(rng):
    clicks = ''
    for _ in range(6):
        x, y = rng.randint(30, 860), rng.randint(60, 700)
        clicks += f'<down:{x},{y}><up:{x},{y}>' + rng.choice(['', 'l', 'H', '+', 'o', 'L', 'f', 'F', 'x', 'i', 'c', 'r', 'e']) + rng.choice(['<Esc>', '<Enter>', 'u', ''])
    return ('<wait:150>\\l1<Esc>\\l2<Esc>\\l3<Esc>\\l4<Esc>\\l5<Esc>\\n<Esc>:export<Enter>:response<Enter><Esc>'
            ':diff @SEED@<Enter>]c]c[c:nodiff<Enter>' + clicks +
            'vjjj0veeey<Esc><Esc>gg/the<Enter>nN<Esc>\\g<Esc>'
            '<C-v>jjjjjjjjT' + rng.choice(['hello', '$\\frac{a}{b}$', 'x' * 400]) + '<C-=><Esc><Esc>'
            ':w @OUT@<Enter><wait:200>u<C-r>uq')


def run(seed_no, data):
    name = f'm{seed_no}'
    d = f'{work}/jobs/{name}'
    shutil.rmtree(d, ignore_errors=True)
    for sub in ('cfg/nomadys-pdf', 'home', 'data', 'cache', 'run'):
        os.makedirs(f'{d}/{sub}')
    os.chmod(f'{d}/run', 0o700)
    open(f'{d}/cfg/nomadys-pdf/config', 'w').write(f'pdf_dirs = {d}\nautosave = false\nbackup = false\n')
    rng = random.Random(seed_no)
    open(f'{d}/in.pdf', 'wb').write(mutate(data, rng))
    sc = script(rng).replace('@SEED@', f'{work}/seed/seed.pdf').replace('@OUT@', 'copy.pdf')
    env = dict(os.environ, HOME=f'{d}/home', XDG_DATA_HOME=f'{d}/data', XDG_CONFIG_HOME=f'{d}/cfg',
               XDG_CACHE_HOME=f'{d}/cache', XDG_RUNTIME_DIR=f'{d}/run', SDL_VIDEODRIVER='offscreen',
               SDL_RENDER_DRIVER='software', ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
    try:
        p = subprocess.run([binary, '--test-out', d, '--test-script', sc, f'{d}/in.pdf'], env=env, timeout=240,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out, code = p.stdout.decode(errors='replace'), p.returncode
    except subprocess.TimeoutExpired as e:
        out, code = (e.stdout or b'').decode(errors='replace'), 'TIMEOUT'
    problems = ['TIMEOUT'] if code == 'TIMEOUT' else []
    problems += ['ASAN ' + m for m in re.findall(r'ERROR: AddressSanitizer: (\S+)', out)]
    problems += ['UBSAN ' + m[:100] for m in re.findall(r'runtime error: ([^\n]+)', out)]
    for block in re.split(r'\n(?=(?:Direct|Indirect) leak)', out):
        if re.match(r'(Direct|Indirect) leak', block) and f'{repo}/src/' in block:
            problems.append('LEAK ' + block.splitlines()[0])
    if isinstance(code, int) and code < 0:
        problems.append(f'SIGNAL {-code}')
    if problems:
        open(f'{d}/log.txt', 'w').write(out)
        return seed_no, '; '.join(dict.fromkeys(problems)), d
    shutil.rmtree(d, ignore_errors=True)
    return seed_no, None, d


def main():
    data = make_seed()
    notes = len(re.findall(rb'/Subtype/(?:Highlight|Stamp|Square|Text)', data))
    print(f'seed: {len(data)} bytes, {notes} notes; {count} mutants from seed {first}, {jobs} at a time')
    bad = 0
    with cf.ThreadPoolExecutor(jobs) as ex:
        for no, problem, d in ex.map(lambda n: run(n, data), range(first, first + count)):
            if problem:
                bad += 1
                print(f'mutant {no}: {problem}   ({d}/log.txt)', flush=True)
    print(f'{count} mutants, {bad} with problems')
    return 1 if bad else 0


sys.exit(main())
