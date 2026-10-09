"""Pictures moved, resized, turned and mirrored, by keys and by the mouse.

usage: objects.py [WORKDIR]      (BIN=path to use another binary, e.g. npdf-asan;
       ONLY=part-of-a-name to run some cases, V=1 to print what was read back)

Each case runs the program with a scripted input on a copy of tests/sample.pdf,
saves, and reads the picture's view and rectangle back from the file. The
window is 900 x 1100 and the page fits its width, so a point on the page is
PX pixels. Needs tests/qa/make_corpus.py's pic.png (made here).
"""
import os, re, shutil, subprocess, sys

repo = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '/tmp/nomadys-objects')
binary = os.environ.get('BIN', f'{repo}/npdf')
os.makedirs(work, exist_ok=True)
subprocess.run(['python3', f'{repo}/tests/qa/make_corpus.py', f'{work}/corpus'], check=True, stdout=subprocess.DEVNULL)
PIC = f'{work}/corpus/pic.png'
PX = 865 / 595  # pixels per point

# the picture, at 48 x 30 pt, made bigger (20 steps of 5%: 127 x 80 pt), done;
# its top left is at about (448, 194) px, its centre at (541, 252)
SETUP = f'<C-v>:image {PIC}<Enter>20+jjjjjjjjjjjjjjjjjjjjjjjjj<Enter><Esc>'
CENTRE = (541, 252)
SAVE = ':w<Enter><wait:300>q'


def run(name, script, src=None):
    d = f'{work}/{name}'
    shutil.rmtree(d, ignore_errors=True)
    for sub in ('cfg/nomadys-pdf', 'home', 'data', 'cache', 'run'):
        os.makedirs(f'{d}/{sub}')
    os.chmod(f'{d}/run', 0o700)
    open(f'{d}/cfg/nomadys-pdf/config', 'w').write(f'pdf_dirs = {d}\n')
    pdf = f'{d}/in.pdf'
    shutil.copy(src or f'{repo}/tests/sample.pdf', pdf)
    env = dict(os.environ, HOME=f'{d}/home', XDG_DATA_HOME=f'{d}/data', XDG_CONFIG_HOME=f'{d}/cfg',
               XDG_CACHE_HOME=f'{d}/cache', XDG_RUNTIME_DIR=f'{d}/run', SDL_VIDEODRIVER='offscreen',
               SDL_RENDER_DRIVER='software', ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
    p = subprocess.run([binary, '--test-out', d, '--test-script', script, pdf], env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=300)
    out = p.stdout.decode(errors='replace')
    leaks = [b for b in re.split(r'\n(?=(?:Direct|Indirect) leak)', out)  # only ours: the system libraries leak a little
             if re.match(r'(Direct|Indirect) leak', b) and f'{repo}/src/' in b]
    bad = (p.returncode != 0 and 'LeakSanitizer' not in out) or 'ERROR: AddressSanitizer' in out or \
        'runtime error' in out or bool(leaks)
    if bad:
        print(f'  [{name}] exit {p.returncode}\n{out[-1500:]}')
    return pdf, not bad


def pictures(pdf):
    """[view fields and rectangle] of every picture (stamp) in the file."""
    data = open(pdf, 'rb').read()
    found = []
    for m in re.finditer(rb'/Subtype/Stamp', data):
        obj = data[data.rfind(b' obj', 0, m.start()):data.find(b'endobj', m.start())]
        r = re.search(rb'/Rect\[([^\]]*)\]', obj)
        v = re.search(rb'/NomadysImage\(([^)]*)\)', obj)
        f = [float(x) for x in v.group(1).split()] if v else [0, 0, 1, 1, 0, 0, 1, 1, 0]
        f += [0] * (14 - len(f))
        found.append({'box': f[0:4], 'crop': f[4:8], 'turns': int(f[8]), 'fh': int(f[9]), 'fv': int(f[10]),
                      'angle': f[11], 'rw': f[12], 'rh': f[13], 'rect': [float(x) for x in r.group(1).split()]})
    return found


fails = 0


def check(name, script, test, src=None):
    global fails
    if os.environ.get('ONLY') and os.environ['ONLY'] not in name:
        return
    pdf, ok = run(name, script, src)
    pics = pictures(pdf) if ok else []
    if os.environ.get('V'):
        print(pics)
    problem = None if ok else 'crashed'
    if ok:
        try:
            test(pics)
        except AssertionError as e:
            problem = f'{e}'
        except IndexError:
            problem = 'no picture in the file'
    if problem:
        fails += 1
    print(f'{name:28s} {"OK" if not problem else "FAIL: " + problem}')


def near(a, b, tol, what=''):
    assert abs(a - b) <= tol, f'{what} {a:.2f} != {b:.2f} (+-{tol})'


def size(p):
    r = p['rect']
    return r[2] - r[0], r[3] - r[1]


def centre_y(p):
    return (p['rect'][1] + p['rect'][3]) / 2


def original():
    pdf, ok = run('original', SETUP + SAVE)
    return pictures(pdf)[0]


def main():
    base = original()
    bw, bh = size(base)
    near(bw, 127, 1.5, 'width')
    sel = f'<down:{CENTRE[0]},{CENTRE[1]}><up:{CENTRE[0]},{CENTRE[1]}>'  # a click selects it

    check('click-selects-and-flips', SETUP + sel + 'f<Enter>' + SAVE,
          lambda p: (near(p[0]['fh'], 1, 0), near(p[0]['fv'], 0, 0), near(p[0]['angle'], 0, 0)))
    check('flip-both', SETUP + sel + 'fF<Enter>' + SAVE,
          lambda p: (near(p[0]['fh'], 1, 0), near(p[0]['fv'], 1, 0)))
    check('flip-twice-is-none', SETUP + sel + 'ff<Enter>' + SAVE,
          lambda p: (near(p[0]['fh'], 0, 0), near(p[0]['fv'], 0, 0)))
    check('flip-undo', SETUP + sel + 'f<Enter>u' + SAVE,
          lambda p: near(p[0]['fh'], 0, 0))
    check('flip-esc', SETUP + sel + 'fF<Esc>' + SAVE,
          lambda p: (near(p[0]['fh'], 0, 0), near(p[0]['fv'], 0, 0)))
    check('turn-after-mirror', SETUP + sel + 'fr<Enter>' + SAVE,  # a mirror, then a turn clockwise: the other way
          lambda p: (near(p[0]['turns'], 3, 0), near(p[0]['fh'], 1, 0)))
    check('keys-angle', SETUP + sel + 'o5L<Enter><Enter>' + SAVE,
          lambda p: (near(p[0]['angle'], 75, 0.01), near(p[0]['rw'] > 0, 1, 0)))
    check('keys-angle-small-steps', SETUP + sel + 'o3l2h<Enter><Enter>' + SAVE,
          lambda p: near(p[0]['angle'], 1, 0.01))
    check('keys-angle-ccw', SETUP + sel + 'oH<Enter><Enter>' + SAVE,
          lambda p: near(p[0]['angle'], -15, 0.01))
    check('keys-angle-upright', SETUP + sel + 'oLLu<Enter><Enter>' + SAVE,
          lambda p: (near(p[0]['angle'], 0, 0), near(size(p[0])[0], bw, 0.5)))
    check('keys-angle-esc', SETUP + sel + 'oLL<Esc><Enter>' + SAVE,
          lambda p: near(p[0]['angle'], 0, 0))
    check('keys-angle-undo', SETUP + sel + 'oLL<Enter><Enter>u' + SAVE,
          lambda p: near(p[0]['angle'], 0, 0))
    check('mirror-turns-the-angle', SETUP + sel + 'oLLf<Enter><Enter>' + SAVE,
          lambda p: (near(p[0]['angle'], -30, 0.01), near(p[0]['fh'], 1, 0)))
    check('angle-then-quarter-turn', SETUP + sel + 'oLLr<Enter><Enter>' + SAVE,
          lambda p: (near(p[0]['angle'], 30, 0.01), near(p[0]['turns'], 1, 0)))
    check('angle-wraps', SETUP + sel + 'o10L<Enter><Enter>' + SAVE,  # 150 deg
          lambda p: near(p[0]['angle'], 150, 0.01))
    check('angle-wraps-past-180', SETUP + sel + 'o13L<Enter><Enter>' + SAVE,  # 195 -> -165
          lambda p: near(p[0]['angle'], -165, 0.01))
    check('crop-after-mirror', SETUP + sel + 'fch<Enter><Enter>' + SAVE,  # the left side as seen is the picture's right
          lambda p: (near(p[0]['crop'][0], 0, 0), near(p[0]['crop'][2] < 1, 1, 0)))
    check('crop-refused-when-turned', SETUP + sel + 'oL<Enter>chh<Enter><Enter>' + SAVE,
          lambda p: (near(p[0]['crop'][0], 0, 0), near(p[0]['crop'][2], 1, 0)))

    # the mouse
    x, y = CENTRE
    check('mouse-move', SETUP + f'<drag:{x},{y},{x + 100},{y + 150}>' + '<Enter>' + SAVE,
          lambda p: (near(p[0]['rect'][0] - base['rect'][0], 100 / PX, 1.5, 'dx'),
                     near(base['rect'][3] - p[0]['rect'][3], 150 / PX, 1.5, 'dy'), near(size(p[0])[0], bw, 0.01)))
    check('mouse-move-esc', SETUP + f'<drag:{x},{y},{x + 100},{y + 150}>' + '<Esc>' + SAVE,
          lambda p: near(p[0]['rect'][0], base['rect'][0], 0.01))
    check('mouse-move-undo', SETUP + f'<drag:{x},{y},{x + 100},{y + 150}>' + '<Enter>u' + SAVE,
          lambda p: near(p[0]['rect'][0], base['rect'][0], 0.01))
    check('mouse-click-no-change', SETUP + sel + '<Enter>' + SAVE,
          lambda p: near(p[0]['rect'][0], base['rect'][0], 0.01))
    # the bottom right corner is at about (634, 310) px
    check('mouse-corner-proportions', SETUP + sel + '<drag:634,310,730,350><Enter>' + SAVE,
          lambda p: near(size(p[0])[0] / size(p[0])[1], bw / bh, 0.02, 'ratio'))
    check('mouse-corner-grows', SETUP + sel + '<drag:634,310,730,350><Enter>' + SAVE,
          lambda p: assert_true(size(p[0])[0] > bw + 20, 'did not grow'))
    check('mouse-side-free', SETUP + sel + '<drag:634,252,734,252><Enter>' + SAVE,
          lambda p: (near(size(p[0])[0], bw + 100 / PX, 2, 'width'), near(size(p[0])[1], bh, 0.5, 'height')))
    check('mouse-side-crossing-mirrors', SETUP + sel + '<drag:634,252,400,252><Enter>' + SAVE,
          lambda p: (near(p[0]['fh'], 1, 0), near(p[0]['fv'], 0, 0), near(size(p[0])[0], (448 - 400) / PX, 2)))
    check('mouse-bottom-crossing-mirrors', SETUP + sel + '<drag:541,310,541,100><Enter>' + SAVE,
          lambda p: (near(p[0]['fv'], 1, 0), near(p[0]['fh'], 0, 0)))
    # the knob is 24 px above the middle of the top edge: (541, 170)
    check('mouse-knob-turns', SETUP + sel + '<drag:541,170,641,252><Enter>' + SAVE,
          lambda p: near(p[0]['angle'], 90, 3, 'angle'))
    check('mouse-knob-left', SETUP + sel + '<drag:541,170,441,252><Enter>' + SAVE,
          lambda p: near(p[0]['angle'], -90, 3, 'angle'))
    check('mouse-rotate-mode-turns', SETUP + sel + 'o<drag:641,252,541,352><Enter><Enter>' + SAVE,
          lambda p: near(p[0]['angle'], 90, 1.5, 'angle'))
    check('mouse-knob-esc', SETUP + sel + '<drag:541,170,641,252><Esc>' + SAVE,
          lambda p: near(p[0]['angle'], 0, 0))
    check('mouse-knob-undo', SETUP + sel + '<drag:541,170,641,252><Enter>u' + SAVE,
          lambda p: near(p[0]['angle'], 0, 0))
    check('mouse-turned-then-moved', SETUP + sel + '<drag:541,170,641,252><drag:541,252,541,352><Enter>' + SAVE,
          lambda p: (near(p[0]['angle'], 90, 3), near(centre_y(base) - centre_y(p[0]), 100 / PX, 3, 'down')))
    check('mouse-turned-then-resized', SETUP + sel + '<drag:541,170,641,252><drag:726,252,800,252><Enter>' + SAVE,
          lambda p: near(p[0]['angle'], 90, 1.5))
    check('click-elsewhere-ends', SETUP + sel + 'f<down:300,700><up:300,700>' + SAVE,
          lambda p: near(p[0]['fh'], 1, 0))

    # the file, opened again
    pdf, ok = run('turned', SETUP + sel + 'o4L<Enter><Enter>' + SAVE)  # 60 degrees
    pdf2 = f'{work}/turned-again.pdf'
    shutil.copy(pdf, pdf2)
    check('reopen-turn-more', sel + 'o2L<Enter><Enter>' + SAVE, lambda p: near(p[0]['angle'], 90, 0.01), src=pdf2)
    check('reopen-mirror', sel + 'f<Enter>' + SAVE,
          lambda p: (near(p[0]['fh'], 1, 0), near(p[0]['angle'], -60, 0.01)), src=pdf2)

    # an older file: the view string without the newer fields
    data = open(pdf2, 'rb').read()
    m = re.search(rb'NomadysImage\(([^)]*)\)', data)
    fields = m.group(1).split()
    old = b' '.join(fields[:9])
    old += b' ' * (len(m.group(1)) - len(old))
    older = f'{work}/older.pdf'
    open(older, 'wb').write(data[:m.start(1)] + old + data[m.end(1):])
    check('older-file', sel + 'oL<Enter><Enter>' + SAVE, lambda p: near(p[0]['angle'], 15, 0.01), src=older)

    print('failures:', fails)
    return fails


def assert_true(c, msg):
    assert c, msg


sys.exit(1 if main() else 0)
