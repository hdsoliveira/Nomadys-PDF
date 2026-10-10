"""Safety checks: what a hostile file next to a PDF, or inside it, must not be able to do.

usage: security.py [WORKDIR]      (BIN=npdf-asan to use the sanitizer build)

Every payload here only touches a harmless marker file the test makes, so a check that
fails shows its marker and does nothing else.

  synctex     the file name in the SyncTeX data is put in a command: an odd name must not run
  latex       a formula in a comment must not read other files (it is typeset by pdflatex)
  sandbox     the indexing process is shut in: no network, no new programs, no writing outside
  socket      the socket the LaTeX editor talks to is for the owner only
"""
import glob, os, re, shutil, stat, subprocess, sys

repo = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '/tmp/nomadys-security')
binary = os.environ.get('BIN', f'{repo}/npdf')
os.makedirs(work, exist_ok=True)
fails = 0


def setup(name, config=''):
    d = f'{work}/{name}'
    shutil.rmtree(d, ignore_errors=True)
    for sub in ('cfg/nomadys-pdf', 'home', 'data', 'cache', 'run', 'bin'):
        os.makedirs(f'{d}/{sub}')
    os.chmod(f'{d}/run', 0o700)
    open(f'{d}/cfg/nomadys-pdf/config', 'w').write(f'pdf_dirs = {d}\nautosave = false\nbackup = false\n{config}')
    shutil.copy(f'{repo}/tests/sample.pdf', f'{d}/in.pdf')
    env = dict(os.environ, HOME=f'{d}/home', XDG_DATA_HOME=f'{d}/data', XDG_CONFIG_HOME=f'{d}/cfg',
               XDG_CACHE_HOME=f'{d}/cache', XDG_RUNTIME_DIR=f'{d}/run', SDL_VIDEODRIVER='offscreen',
               SDL_RENDER_DRIVER='software', NPDF_MESSAGES=f'{d}/messages.log', PATH=f'{d}/bin:' + os.environ['PATH'],
               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
    return d, env


def run(d, env, script, args=None):
    p = subprocess.run([binary, '--test-out', d, '--test-script', script] + (args or [f'{d}/in.pdf']), env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    out = p.stdout.decode(errors='replace')
    leaks = [b for b in re.split(r'\n(?=(?:Direct|Indirect) leak)', out)
             if re.match(r'(Direct|Indirect) leak', b) and f'{repo}/src/' in b]
    assert not ((p.returncode != 0 and 'LeakSanitizer' not in out) or 'ERROR: AddressSanitizer' in out or
                'runtime error' in out or leaks), f'crashed:\n{out[-800:]}'
    log = f'{d}/messages.log'
    return open(log).read().splitlines() if os.path.exists(log) else []


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
    print(f'{name:36s} {"OK" if not problem else "FAIL: " + problem}')


def fake_synctex(d, source_name):
    """A `synctex` that answers any inverse search with this file name (read from a file, so that
    the name cannot interfere with the script itself)."""
    open(f'{d}/answer-name', 'w').write(source_name)
    open(f'{d}/bin/synctex', 'w').write(f'#!/bin/sh\nprintf "Input:%s\\nLine:3\\nColumn:0\\n" "$(cat {d}/answer-name)"\n')
    os.chmod(f'{d}/bin/synctex', 0o755)
    open(f'{d}/in.synctex.gz', 'wb').write(b'')


def synctex_odd_name():
    d, env = setup('synctex-odd')
    marker = f'{d}/MARKER-RAN'
    # a name that, put in the editor command between quotes, would end the quote and run something
    fake_synctex(d, f"{d}/x'; touch {marker}; echo '")
    cmd = f"printf %s '%f' >> {d}/editor.log"
    open(f'{d}/cfg/nomadys-pdf/config', 'a').write(f'synctex_editor = {cmd}\n')
    msgs = run(d, env, '<wait:200><dclick:300,300><wait:800>q')
    assert not os.path.exists(marker), 'the odd file name ran a command'
    assert any('not opened' in m for m in msgs), f'no refusal: {msgs}'
    assert not os.path.exists(f'{d}/editor.log'), 'the editor was started for an odd name'


def synctex_plain_name():
    d, env = setup('synctex-plain')
    tex = f'{d}/paper source.tex'  # a space is fine
    open(tex, 'w').write('x\n')
    fake_synctex(d, tex)
    open(f'{d}/cfg/nomadys-pdf/config', 'a').write(f"synctex_editor = printf %s '%f' >> {d}/editor.log\n")
    run(d, env, '<wait:200><dclick:300,300><wait:800>q')
    got = open(f'{d}/editor.log').read() if os.path.exists(f'{d}/editor.log') else None
    assert got == tex, f'the editor was not given the plain file: {got!r}'


def synctex_missing_file():
    d, env = setup('synctex-missing')
    fake_synctex(d, f'{d}/not-there.tex')
    open(f'{d}/cfg/nomadys-pdf/config', 'a').write(f"synctex_editor = printf %s '%f' >> {d}/editor.log\n")
    run(d, env, '<wait:200><dclick:300,300><wait:800>q')
    assert not os.path.exists(f'{d}/editor.log'), 'the editor was started for a file that is not there'


def latex_cannot_read_files():
    d, env = setup('latex')
    secret = f'{d}/secret.txt'
    open(secret, 'w').write('SECRETMARKER from another file\n')
    # a comment whose formula asks LaTeX to read a file; a plain formula next to it must still work
    script = ('<wait:200>vjjj0veeec$\\input{' + secret + '}$ and $\\frac{a}{b}$<Enter><Esc><Esc>'
              'vjjj0veeec$\\frac{x}{y}$<Enter><wait:6000>q')
    run(d, env, script)
    pdfs = glob.glob(f'{d}/cache/nomadys-pdf/math/*.pdf')
    seen = ''
    for pdf in pdfs:
        seen += subprocess.run(['pdftotext', pdf, '-'], capture_output=True, text=True).stdout
    assert 'SECRETMARKER' not in seen.replace(' ', ''), 'a formula read another file'
    assert pdfs, 'no formula was typeset at all (is pdflatex installed?)'


def sandbox_of_the_indexer():
    d, env = setup('sandbox')
    probe = f'{d}/probe.txt'
    open(probe, 'w').write('canary\n')
    env.update(NPDF_SANDBOX_CHECK='1', NPDF_SANDBOX_PROBE=probe, ASAN_OPTIONS='detect_leaks=0')  # LeakSanitizer needs ptrace, which the sandbox denies
    out = ''
    for part in '012':  # which worker gets the PDF depends on its path
        out += subprocess.run([binary, '--index-worker', f'{d}/index', part], env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=120).stdout.decode()
    line = [l for l in out.splitlines() if l.startswith('S ')]
    assert line, f'the worker did not report: {out[:200]}'
    if 'files-confined=0' in line[0] or 'network-closed=0' in line[0]:
        return 'skipped: this kernel has no Landlock or seccomp'
    assert 'create-elsewhere=blocked' in line[0] and 'write-existing=blocked' in line[0], f'it can write: {line[0]}'
    assert 'socket=blocked' in line[0], f'it can open a network socket: {line[0]}'
    assert open(probe).read() == 'canary\n', 'a file was changed'
    assert glob.glob(f'{d}/index/*.gz'), 'the sandboxed worker made no index'


def socket_is_private():
    import tempfile
    d, env = setup('socket')
    short = tempfile.mkdtemp(prefix='npdf', dir='/tmp')  # a socket path must be short
    env['XDG_RUNTIME_DIR'] = short
    try:
        p = subprocess.Popen([binary, '--test-out', d, '--test-script', '<wait:3000>', f'{d}/in.pdf'], env=env,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sock, mode = f'{short}/nomadys-pdf.sock', None
        for _ in range(50):
            if os.path.exists(sock):
                mode = stat.S_IMODE(os.stat(sock).st_mode)
                break
            subprocess.run(['sleep', '0.1'])
        p.wait()
    finally:
        shutil.rmtree(short, ignore_errors=True)
    assert mode is not None, 'no socket'
    assert mode & 0o077 == 0, f'others can reach the socket (mode {oct(mode)})'


case('synctex-odd-name-is-refused', synctex_odd_name)
case('synctex-plain-name-opens', synctex_plain_name)
case('synctex-missing-file-is-refused', synctex_missing_file)
case('latex-cannot-read-files', latex_cannot_read_files)
case('indexer-is-shut-in', sandbox_of_the_indexer)
case('socket-is-private', socket_is_private)
print('failures:', fails)
sys.exit(1 if fails else 0)
