"""Run nomadys-pdf-asan (sanitizer build) and classify the result.

usage: check.py WORKDIR NAME TIMEOUT -- nomadys-pdf arguments...
Prints one line: OK, or FAIL with the AddressSanitizer / UBSan / leak /
timeout findings. Leaks count only when a stack frame is in src/ (system
libraries such as the GPU driver leak a little at exit). Full log in
WORKDIR/logs/NAME.log.
"""
import os, re, subprocess, sys
repo = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
work, name, timeout = os.path.abspath(sys.argv[1]), sys.argv[2], float(sys.argv[3])
args = sys.argv[sys.argv.index('--') + 1:]
for d in ('logs', 'data', 'cfg', f'run/{name}'):
    os.makedirs(f'{work}/{d}', exist_ok=True)
env = dict(os.environ, XDG_DATA_HOME=f'{work}/data', XDG_CONFIG_HOME=f'{work}/cfg',
           XDG_RUNTIME_DIR=f'{work}/run/{name}', SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
           ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
binary = os.environ.get('BIN', f'{repo}/nomadys-pdf-asan')
try:
    p = subprocess.run([binary] + args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    out, code = p.stdout.decode(errors='replace'), p.returncode
except subprocess.TimeoutExpired as e:
    out, code = (e.stdout or b'').decode(errors='replace'), 'TIMEOUT'
open(f'{work}/logs/{name}.log', 'w').write(out)
problems = ['TIMEOUT'] if code == 'TIMEOUT' else []
problems += ['ASAN ' + m for m in re.findall(r'ERROR: AddressSanitizer: (\S+)', out)]
problems += ['UBSAN ' + m[:90] for m in re.findall(r'runtime error: ([^\n]+)', out)]
for block in re.split(r'\n(?=(?:Direct|Indirect) leak)', out):
    if re.match(r'(Direct|Indirect) leak', block) and f'{repo}/src/' in block:
        problems.append('LEAK ' + block.splitlines()[0])
if isinstance(code, int) and code < 0:
    problems.append(f'SIGNAL {-code}')
print(f'{name:32s} {"OK" if not problems else "FAIL: " + "; ".join(dict.fromkeys(problems))}')
sys.exit(1 if problems else 0)
