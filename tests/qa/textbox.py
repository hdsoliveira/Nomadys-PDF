"""Free text: written, resized, moved, written again, undone, by keys and by the mouse.

usage: textbox.py [WORKDIR]      (BIN=path to use another binary, e.g. npdf-asan;
       ONLY=part-of-a-name to run some cases, V=1 to print what was read back)

Each case runs the program with a scripted input on a copy of tests/sample.pdf,
saves, and reads the text boxes back from the file (their rectangle, size,
width and text). The window is 900 x 1100 and the page fits its width.
"""
import os, re, shutil, subprocess, sys

repo = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '/tmp/nomadys-textbox')
binary = os.environ.get('BIN', f'{repo}/npdf')
os.makedirs(work, exist_ok=True)
PX = 865 / 595  # pixels per point
SAVE = ':w<Enter><wait:300>q'
NEW = '<C-v>jjjjjjjjjT'          # the free cursor, down to about 126 pt from the top: the text starts there
INSIDE = (520, 215)              # a point inside the box made by NEW (its left is at about 450 px)


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
    p = subprocess.run([binary, '--test-out', d, '--test-script', script_of(script), pdf], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    out = p.stdout.decode(errors='replace')
    leaks = [b for b in re.split(r'\n(?=(?:Direct|Indirect) leak)', out)  # only ours: the system libraries leak a little
             if re.match(r'(Direct|Indirect) leak', b) and f'{repo}/src/' in b]
    bad = (p.returncode != 0 and 'LeakSanitizer' not in out) or 'ERROR: AddressSanitizer' in out or \
        'runtime error' in out or bool(leaks)
    if bad:
        print(f'  [{name}] exit {p.returncode}\n{out[-1500:]}')
    return pdf, not bad


def script_of(s):
    return s


PDFDOC = {0x80: '•', 0x81: '†', 0x82: '‡', 0x83: '…', 0x84: '—', 0x85: '–', 0x86: 'ƒ', 0x87: '⁄', 0x88: '‹',
          0x89: '›', 0x8A: '−', 0x8B: '‰', 0x8C: '„', 0x8D: '“', 0x8E: '”', 0x8F: '‘', 0x90: '’', 0x91: '‚',
          0x92: '™', 0x93: 'ﬁ', 0x94: 'ﬂ', 0x95: 'Ł', 0x96: 'Œ', 0x97: 'Š', 0x98: 'Ÿ', 0x99: 'Ž', 0x9A: 'ı',
          0x9B: 'ł', 0x9C: 'œ', 0x9D: 'š', 0x9E: 'ž', 0xA0: '€'}


def unescape(raw):
    """A PDF string: PDFDocEncoding bytes with \\n \\( \\) \\\\ escapes, or UTF-16BE with a BOM."""
    out, i = bytearray(), 0
    while i < len(raw):
        c = raw[i:i + 1]
        if c == b'\\' and i + 1 < len(raw):
            i += 1
            e = raw[i:i + 1]
            out += {b'n': b'\n', b'r': b'\r', b't': b'\t'}.get(e, e)
        else:
            out += c
        i += 1
    if out.startswith(b'\xfe\xff'):
        return bytes(out[2:]).decode('utf-16-be')
    return ''.join(PDFDOC.get(c, chr(c)) for c in out)


def page_annots(data):
    """The annotation dictionaries (as bytes) the first page lists: inline or by reference."""
    m = re.search(rb'/Annots\[', data)
    if not m:
        return []
    depth, i, items = 1, m.end(), []
    start = i
    while depth and i < len(data):
        c = data[i:i + 1]
        if c == b'[':
            depth += 1
        elif c == b']':
            depth -= 1
        i += 1
    body = data[start:i - 1]
    j = 0
    while j < len(body):
        ref = re.match(rb'\s*(\d+) 0 R', body[j:])
        if ref:
            n = int(ref.group(1))
            o = re.search(rb'(?:^|[\r\n])%d 0 obj(.*?)endobj' % n, data, re.S)
            items.append(o.group(1) if o else b'')
            j += ref.end()
        elif body[j:j + 2] == b'<<':
            d, k = 0, j
            while k < len(body):
                if body[k:k + 2] == b'<<':
                    d += 1
                    k += 2
                elif body[k:k + 2] == b'>>':
                    d -= 1
                    k += 2
                    if d == 0:
                        break
                else:
                    k += 1
            items.append(body[j:k])
            j = k
        else:
            j += 1
    return items


def boxes(pdf):
    """[dict] of every text box the page lists, deleted ones too (marked)."""
    data = open(pdf, 'rb').read()
    found = []
    for obj in page_annots(data):
        m = re.search(rb'/NomadysText\(([^)]*)\)', obj)
        if not m:
            continue
        r = re.search(rb'/Rect\[([^\]]*)\]', obj)
        c = re.search(rb'/Contents\(((?:[^()\\]|\\.)*)\)', obj)
        f = re.search(rb'/F (\d+)', obj)
        size, width = [float(x) for x in m.group(1).split()]
        rect = [float(x) for x in r.group(1).split()]
        found.append({'size': size, 'width': width, 'rect': rect, 'left': rect[0], 'top': rect[3],
                      'height': rect[3] - rect[1], 'text': unescape(c.group(1)) if c else '',
                      'deleted': b'NomadysDeleted(1)' in obj, 'hidden': bool(f and int(f.group(1)) & 2)})
    return found


fails = 0


def check(name, script, test, src=None):
    global fails
    if os.environ.get('ONLY') and os.environ['ONLY'] not in name:
        return
    pdf, ok = run(name, script, src)
    found = [b for b in boxes(pdf) if not (b['deleted'] or b['hidden'])] if ok else []
    if os.environ.get('V'):
        print(found)
    problem = None if ok else 'crashed'
    if ok:
        try:
            test(found)
        except AssertionError as e:
            problem = f'{e}'
        except IndexError:
            problem = 'no text box in the file'
    if problem:
        fails += 1
    print(f'{name:30s} {"OK" if not problem else "FAIL: " + problem}')


def near(a, b, tol, what=''):
    assert abs(a - b) <= tol, f'{what} {a:.2f} != {b:.2f} (+-{tol})'


def is_(a, b, what=''):
    assert a == b, f'{what} {a!r} != {b!r}'


def main():
    LINE = 1.2 * 12  # one line of 12 pt letters
    click = f'<down:{INSIDE[0]},{INSIDE[1]}><up:{INSIDE[0]},{INSIDE[1]}>'

    check('writes', NEW + 'Hello world<Esc>' + SAVE,
          lambda b: (is_(len(b), 1), is_(b[0]['text'], 'Hello world'), near(b[0]['size'], 12, 0),
                     near(b[0]['width'], 200, 0), near(b[0]['height'], LINE + 6, 0.5, 'height')))
    check('accents-and-signs', NEW + 'ação “x” — € ß<Esc>' + SAVE,
          lambda b: is_(b[0]['text'], 'ação “x” — € ß'))
    check('grows-with-the-text', NEW + 'one two three four five six seven eight nine ten eleven twelve thirteen '
          'fourteen fifteen sixteen seventeen<Esc>' + SAVE,
          lambda b: assert_true(b[0]['height'] > 2 * LINE + 6 - 1, 'did not grow'))
    check('new-line', NEW + 'a<Enter>b<Enter>c<Esc>' + SAVE,
          lambda b: (is_(b[0]['text'], 'a\nb\nc'), near(b[0]['height'], 3 * LINE + 6, 0.5)))
    check('letters-bigger', NEW + 'abc<C-=><C-=><Esc>' + SAVE,
          lambda b: near(b[0]['size'], 14.5, 0.01))
    check('letters-smaller', NEW + 'abc<C--><Esc>' + SAVE,
          lambda b: near(b[0]['size'], 11, 0.01))
    check('width-from-the-rectangle', '<C-v>jjjjjjjjjvllllllllllT' + 'abc<Esc>' + SAVE,
          lambda b: near(b[0]['width'], 72, 30, 'width'))  # ten steps of the cursor, at least six letters wide
    check('left-and-top', NEW + 'abc<Esc>' + SAVE,
          lambda b: (near(b[0]['top'], 842 - 126, 20, 'top'), assert_true(b[0]['left'] >= 0, 'left')))
    check('empty-leaves-nothing', NEW + '<Esc>' + SAVE, lambda b: is_(len(b), 0))
    check('erased-leaves-nothing', NEW + 'abc<BS><BS><BS><Esc>' + SAVE, lambda b: is_(len(b), 0))
    check('backspace-and-arrows', NEW + 'abc<Left><BS><Esc>' + SAVE, lambda b: is_(b[0]['text'], 'ac'))
    check('delete-forward', NEW + 'abc<Left><Left><Del><Esc>' + SAVE, lambda b: is_(b[0]['text'], 'ac'))
    check('word-erased', NEW + 'one two<C-w><Esc>' + SAVE, lambda b: is_(b[0]['text'], 'one '))
    check('home-and-end', NEW + 'abc<Home>X<End>Y<Esc>' + SAVE, lambda b: is_(b[0]['text'], 'XabcY'))
    check('up-and-down', NEW + 'abc<Enter>de<Up>X<Esc>' + SAVE, lambda b: is_(b[0]['text'], 'abXc\nde'))
    check('undo-hides-it', NEW + 'abc<Esc>u' + SAVE, lambda b: is_(len(b), 0))
    check('undo-redo', NEW + 'abc<Esc>u<C-r>' + SAVE, lambda b: is_(b[0]['text'], 'abc'))
    check('not-in-the-font', NEW + 'a日b<Esc>' + SAVE, lambda b: is_(b[0]['text'], 'a日b'))

    # written again
    check('write-again', NEW + 'Hello<Esc>' + click + 'i world<Esc>' + SAVE,
          lambda b: is_(b[0]['text'], 'Hello world'))
    check('write-again-undo', NEW + 'Hello<Esc>' + click + 'i world<Esc>u' + SAVE,
          lambda b: is_(b[0]['text'], 'Hello'))
    check('write-again-redo', NEW + 'Hello<Esc>' + click + 'i world<Esc>u<C-r>' + SAVE,
          lambda b: is_(b[0]['text'], 'Hello world'))
    check('write-again-erase-all', NEW + 'Hello<Esc>' + click + 'i<BS><BS><BS><BS><BS><Esc>' + SAVE,
          lambda b: is_(len(b), 0))
    check('write-again-erase-undo', NEW + 'Hello<Esc>' + click + 'i<BS><BS><BS><BS><BS><Esc>u' + SAVE,
          lambda b: is_(b[0]['text'], 'Hello'))

    # moved and resized
    check('width-keys', NEW + 'Hello<Esc>' + click + 'LL<Enter>' + SAVE,
          lambda b: assert_true(b[0]['width'] > 200, 'width'))
    check('width-narrower', NEW + 'Hello<Esc>' + click + 'HH<Enter>' + SAVE,
          lambda b: assert_true(b[0]['width'] < 200, 'width'))
    check('letters-keys', NEW + 'Hello<Esc>' + click + '++<Enter>' + SAVE,
          lambda b: assert_true(b[0]['size'] > 12, 'size'))
    check('esc-puts-back', NEW + 'Hello<Esc>' + click + 'LL++l<Esc>' + SAVE,
          lambda b: (near(b[0]['width'], 200, 0), near(b[0]['size'], 12, 0)))
    check('resize-undo', NEW + 'Hello<Esc>' + click + 'LL<Enter>u' + SAVE,
          lambda b: near(b[0]['width'], 200, 0))
    check('move-keys', NEW + 'Hello<Esc>' + click + 'llll<Enter>' + SAVE,
          lambda b: (assert_true(b[0]['left'] > 297.5 + 1, 'did not move'), is_(b[0]['text'], 'Hello')))
    check('move-undo', NEW + 'Hello<Esc>' + click + 'llll<Enter>u' + SAVE,
          lambda b: near(b[0]['left'], 297.5, 1))
    x, y = INSIDE
    check('mouse-move', NEW + 'Hello<Esc>' + f'<drag:{x},{y},{x + 100},{y + 100}>' + '<Enter>' + SAVE,
          lambda b: (near(b[0]['left'], 297.5 + 100 / PX, 3, 'dx'), near(b[0]['width'], 200, 0)))
    check('mouse-width', NEW + 'Hello<Esc>' + click + '<drag:738,215,838,215><Enter>' + SAVE,
          lambda b: near(b[0]['width'], 200 + 100 / PX, 6, 'width'))
    check('mouse-move-esc', NEW + 'Hello<Esc>' + f'<drag:{x},{y},{x + 100},{y + 100}>' + '<Esc>' + SAVE,
          lambda b: near(b[0]['left'], 297.5, 1))
    check('delete', NEW + 'Hello<Esc>' + click + 'x' + SAVE, lambda b: is_(len(b), 0))
    check('delete-undo', NEW + 'Hello<Esc>' + click + 'xu' + SAVE, lambda b: is_(b[0]['text'], 'Hello'))

    # two boxes, and the file
    check('two-boxes', NEW + 'one<Esc>jjjjjjjjjjjjjjjjjjjjjjT' + 'two<Esc>' + SAVE, lambda b: is_(len(b), 2))
    # :w name.pdf writes a copy next to the PDF; a name that is there is refused, :w! replaces it
    global fails
    pdf, ok = run('copy', NEW + 'abc<Esc>:w copy.pdf<Enter><wait:300>jjjjjjjjjjjjjjjjjjjjjjT' + 'two<Esc>:w copy.pdf<Enter><wait:300>q')
    got = boxes(f'{work}/copy/copy.pdf') if os.path.exists(f'{work}/copy/copy.pdf') else []
    problem = None if ok and len(got) == 1 else f'{len(got)} boxes in the copy (1 expected: the second :w is refused)'
    pdf, ok = run('copy-replace', NEW + 'abc<Esc>:w copy.pdf<Enter><wait:300>jjjjjjjjjjjjjjjjjjjjjjT' + 'two<Esc>:w! copy.pdf<Enter><wait:300>q')
    got = boxes(f'{work}/copy-replace/copy.pdf') if os.path.exists(f'{work}/copy-replace/copy.pdf') else []
    problem = problem or (None if ok and len(got) == 2 else f'{len(got)} boxes after :w! (2 expected)')
    fails += bool(problem)
    print(f'{"save-copy":30s} {"OK" if not problem else "FAIL: " + problem}')
    pdf, ok = run('size', NEW + 'x<Esc>' + SAVE)
    small = os.path.getsize(pdf)
    long = 'Hello world, ação and a long line that must wrap to the next lines of the box. ' * 4
    pdf, ok = run('size-long', NEW + long + '<Esc>' + SAVE)
    per_char = (os.path.getsize(pdf) - small) / (len(long) - 1)
    check('small-file', NEW + 'x<Esc>' + SAVE, lambda b: assert_true(per_char < 6, f'{per_char:.1f} bytes a letter'))
    print(f'  ({per_char:.1f} bytes a letter)')
    print('failures:', fails)
    return fails


def assert_true(c, msg):
    assert c, msg


sys.exit(1 if main() else 0)
