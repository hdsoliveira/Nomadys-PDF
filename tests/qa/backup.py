"""The backup: the file as it was before the first save of a session, as name.pdf.bak.

usage: backup.py [WORKDIR]      (BIN=path to use another binary, e.g. npdf-asan)

Each case runs the program with a scripted input on a copy of tests/sample.pdf and
looks at the files next to it afterwards.
"""
import os, re, shutil, subprocess, sys

repo = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '/tmp/nomadys-backup')
binary = os.environ.get('BIN', f'{repo}/npdf')
os.makedirs(work, exist_ok=True)
fails = 0
BOX = '<C-v>jjjjjjjjjT{}<Esc><Esc>'
SAMPLE = open(f'{repo}/tests/sample.pdf', 'rb').read()


def run(name, script, config='', src=None, keep=False):
    d = f'{work}/{name}'
    if not keep:
        shutil.rmtree(d, ignore_errors=True)
        for sub in ('cfg/nomadys-pdf', 'home', 'data', 'cache', 'run'):
            os.makedirs(f'{d}/{sub}')
        os.chmod(f'{d}/run', 0o700)
        shutil.copy(src or f'{repo}/tests/sample.pdf', f'{d}/in.pdf')
    open(f'{d}/cfg/nomadys-pdf/config', 'w').write(f'pdf_dirs = {d}\n{config}')
    log = f'{d}/messages.log'
    if os.path.exists(log):
        os.remove(log)
    env = dict(os.environ, HOME=f'{d}/home', XDG_DATA_HOME=f'{d}/data', XDG_CONFIG_HOME=f'{d}/cfg',
               XDG_CACHE_HOME=f'{d}/cache', XDG_RUNTIME_DIR=f'{d}/run', SDL_VIDEODRIVER='offscreen',
               SDL_RENDER_DRIVER='software', NPDF_MESSAGES=log,
               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
    p = subprocess.run([binary, '--test-out', d, '--test-script', script, f'{d}/in.pdf'], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    out = p.stdout.decode(errors='replace')
    leaks = [b for b in re.split(r'\n(?=(?:Direct|Indirect) leak)', out)
             if re.match(r'(Direct|Indirect) leak', b) and f'{repo}/src/' in b]
    bad = (p.returncode != 0 and 'LeakSanitizer' not in out) or 'ERROR: AddressSanitizer' in out or \
        'runtime error' in out or bool(leaks)
    if bad:
        print(f'  [{name}] exit {p.returncode}\n{out[-1500:]}')
    msgs = open(log).read().splitlines() if os.path.exists(log) else []
    return d, msgs, not bad


def case(name, fn):
    global fails
    if os.environ.get('ONLY') and os.environ['ONLY'] not in name:
        return
    try:
        problem = fn()
    except AssertionError as e:
        problem = str(e)
    if problem:
        fails += 1
    print(f'{name:30s} {"OK" if not problem else "FAIL: " + problem}')


def read(path):
    return open(path, 'rb').read() if os.path.exists(path) else None


def save_keeps_the_original():
    d, msgs, ok = run('save', BOX.format('hello') + ':w<Enter><wait:300>q')
    assert ok, 'crashed'
    assert read(f'{d}/in.pdf.bak') == SAMPLE, 'the .bak is not the file as it was'
    assert read(f'{d}/in.pdf') != SAMPLE, 'the file was not saved'
    assert any('kept as in.pdf.bak' in m for m in msgs), f'no message about it: {msgs}'


def one_per_session():
    d, msgs, ok = run('twice', BOX.format('one') + ':w<Enter><wait:300>' + 'jjjjjjjjjjjjjjjjjjjjjjT' + 'two<Esc>:w<Enter><wait:300>q')
    assert ok, 'crashed'
    assert read(f'{d}/in.pdf.bak') == SAMPLE, 'the second save replaced the .bak'
    assert sum('kept as' in m for m in msgs) == 1, 'said more than once'


def a_hard_link_costs_nothing():
    d, msgs, ok = run('link', BOX.format('hello') + ':w<Enter><wait:300>q')
    assert ok, 'crashed'
    bak, now = os.stat(f'{d}/in.pdf.bak'), os.stat(f'{d}/in.pdf')
    assert bak.st_ino != now.st_ino, 'the saved file and the .bak are one file'
    assert bak.st_nlink == 1, 'the .bak is shared'


def autosave_too():
    d, msgs, ok = run('auto', BOX.format('hello') + '<wait:2500>q')
    assert ok, 'crashed'
    assert read(f'{d}/in.pdf') != SAMPLE, 'autosave did not save'
    assert read(f'{d}/in.pdf.bak') == SAMPLE, 'autosave overwrote the file without a .bak'


def next_session_replaces_it():
    d, msgs, ok = run('sessions', BOX.format('first') + ':w<Enter><wait:300>q')
    assert ok, 'crashed'
    after_first = read(f'{d}/in.pdf')
    d, msgs, ok = run('sessions', 'jjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjj<C-v>jjjjjjjjjjjjjjjjjjjjjjT' + 'second<Esc><Esc>:w<Enter><wait:300>q', keep=True)
    assert ok, 'crashed'
    assert read(f'{d}/in.pdf.bak') == after_first, 'the .bak is not the file as the second session found it'


def can_be_off():
    d, msgs, ok = run('off', BOX.format('hello') + ':w<Enter><wait:300>q', config='backup = false\n')
    assert ok, 'crashed'
    assert read(f'{d}/in.pdf') != SAMPLE, 'not saved'
    assert read(f'{d}/in.pdf.bak') is None, 'a .bak was made'


def nothing_changed_nothing_made():
    d, msgs, ok = run('idle', ':w<Enter><wait:300>q')
    assert ok, 'crashed'
    assert read(f'{d}/in.pdf.bak') is None or read(f'{d}/in.pdf.bak') == SAMPLE


case('save-keeps-the-original', save_keeps_the_original)
case('one-per-session', one_per_session)
case('a-hard-link-costs-nothing', a_hard_link_costs_nothing)
case('autosave-too', autosave_too)
case('next-session-replaces-it', next_session_replaces_it)
case('can-be-off', can_be_off)
case('idle-save-is-harmless', nothing_changed_nothing_made)
print('failures:', fails)
sys.exit(1 if fails else 0)
