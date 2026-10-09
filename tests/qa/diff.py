"""`:diff old.pdf`: what changed in the text and in the notes.

usage: diff.py [WORKDIR]      (BIN=path to use another binary, e.g. npdf-asan;
       ONLY=part-of-a-name to run some cases)

Each case runs the program with a scripted input on a copy of tests/sample.pdf;
the messages it showed are read back from a log (NPDF_MESSAGES) and checked.
The window is 900 x 1100.
"""
import os, re, shutil, subprocess, sys

repo = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '/tmp/nomadys-diff')
binary = os.environ.get('BIN', f'{repo}/npdf')
os.makedirs(work, exist_ok=True)
fails = 0
# a highlight over three words with a comment (the text cursor), and a text box (the free cursor)
NOTE = 'vjjj0veeec{}<Enter><Esc><Esc>'
BOX = '<C-v>jjjjjjjjjT{}<Esc><Esc>'
CLICK = '<down:520,215><up:520,215>'   # on that text box


def run(name, script, src=None):
    d = f'{work}/{name}'
    shutil.rmtree(d, ignore_errors=True)
    for sub in ('cfg/nomadys-pdf', 'home', 'data', 'cache', 'run'):
        os.makedirs(f'{d}/{sub}')
    os.chmod(f'{d}/run', 0o700)
    open(f'{d}/cfg/nomadys-pdf/config', 'w').write(f'pdf_dirs = {d}\nautosave = false\n')
    pdf = f'{d}/in.pdf'
    shutil.copy(src or f'{repo}/tests/sample.pdf', pdf)
    env = dict(os.environ, HOME=f'{d}/home', XDG_DATA_HOME=f'{d}/data', XDG_CONFIG_HOME=f'{d}/cfg',
               XDG_CACHE_HOME=f'{d}/cache', XDG_RUNTIME_DIR=f'{d}/run', SDL_VIDEODRIVER='offscreen',
               SDL_RENDER_DRIVER='software', NPDF_MESSAGES=f'{d}/messages.log',
               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
    p = subprocess.run([binary, '--test-out', d, '--test-script', script, pdf], env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=300)
    out = p.stdout.decode(errors='replace')
    leaks = [b for b in re.split(r'\n(?=(?:Direct|Indirect) leak)', out)
             if re.match(r'(Direct|Indirect) leak', b) and f'{repo}/src/' in b]
    bad = (p.returncode != 0 and 'LeakSanitizer' not in out) or 'ERROR: AddressSanitizer' in out or \
        'runtime error' in out or bool(leaks)
    if bad:
        print(f'  [{name}] exit {p.returncode}\n{out[-1500:]}')
    log = f'{d}/messages.log'
    return (open(log).read().splitlines() if os.path.exists(log) else []), not bad


def check(name, script, want, forbid=()):
    """Passes when some message contains every text in `want` and none contains one in `forbid`."""
    global fails
    if os.environ.get('ONLY') and os.environ['ONLY'] not in name:
        return
    msgs, ok = run(name, script)
    problem = None if ok else 'crashed'
    if ok:
        for w in want:
            if not any(w in m for m in msgs):
                problem = f'no message with {w!r}; got {msgs[-6:]}'
                break
        for f in forbid:
            if any(f in m for m in msgs):
                problem = f'unexpected message with {f!r}'
    if problem:
        fails += 1
    print(f'{name:28s} {"OK" if not problem else "FAIL: " + problem}')


def main():
    old = f'{work}/old.pdf'
    shutil.copy(f'{repo}/tests/sample.pdf', old)
    D = f':diff {old}<Enter><wait:400>'

    check('same-file', D, ['no change in the text or the notes'])
    check('text-box-new', BOX.format('a new box') + D + ']c', ['1 change', 'new text box', 'a new box'])
    check('comment-new', NOTE.format('check this') + D + ']c', ['1 change', 'new highlight', 'check this'])
    check('two-notes', BOX.format('one') + NOTE.format('two') + D, ['2 changes'])
    check('removed', BOX.format('check this') + ':w gone.pdf<Enter>' + CLICK + 'x<Esc>' +
          f':diff {work}/removed/gone.pdf<Enter><wait:400>]c', ['text box removed', 'check this'])
    check('changed', BOX.format('Hello') + ':w before.pdf<Enter>' + CLICK + 'i world<Esc><Esc>' +
          f':diff {work}/changed/before.pdf<Enter><wait:400>]c', ['text box changed', 'was “Hello”', 'now “Hello world”'])
    check('undone-is-not-a-change', BOX.format('Hello') + D.replace(old, old) + 'u' + D, ['no change in the text or the notes'])
    print('failures:', fails)
    return fails


sys.exit(1 if main() else 0)
