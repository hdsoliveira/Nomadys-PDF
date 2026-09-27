"""Random key sequences drawn from every keymap, plus text and specials."""
import random, re, sys
seed, n = int(sys.argv[1]), int(sys.argv[2])
work = sys.argv[3] if len(sys.argv) > 3 else '/tmp/nomadys-qa'
random.seed(seed)
src = open(__import__('os').path.expanduser('~/development/Nomadys-PDF/src/input.c')).read()
keys = set(re.findall(r'\{"((?:[^"\\]|\\.)+)", a_', src))
tokens = []
for k in keys:
    k = k.replace('\\\\', '\\').replace('<leader>', '\\').replace('*', random.choice('abcxyz'))
    tokens.append(k)
specials = ['<Esc>', '<Enter>', '<Tab>', '<S-Tab>', '<BS>', '<Del>', '<Up>', '<Down>', '<Left>', '<Right>',
            '<Home>', '<End>', '<PageUp>', '<PageDown>', '<C-a>', '<C-z>', '<C-u>', '<C-w>', '<C-x>', '<C-q>']
words = ['hello', 'ção ü 漢字 🙂', 'x' * 300, ' ', ':', '/', '0', '42', '999999', 'rotate 90', 'zoom 1',
         'zoom 99999', 'e /nonexistent', 'e ~', 'page -5', 'page 0', 'marks', 'reload', 'w', 'q!', 'noh',
         'image /nonexistent.png'] + ['image %s/corpus/%s' % (work, f) for f in ('pic.png', 'opaque.png', 'trunc.png', 'bad.png', 'pic.jpg')] + ['diff %s/corpus/%s' % (work, f) for f in ('text.pdf', 'garbage.pdf', 'many_pages.pdf', 'empty.pdf', 'rotated.pdf')] + ['nodiff', 'present', 'pages', 'recent', 'session', 'theme', 'theme nord', 'theme zzz', 'theme new fz', 'theme new']
out = []
for i in range(n):
    r = random.random()
    if r < 0.55:
        out.append(random.choice(tokens))
    elif r < 0.75:
        out.append(random.choice(specials))
    elif r < 0.92:
        out.append(random.choice(words))
    elif r < 0.96:
        out.append(str(random.randint(1, 30)))
    elif r < 0.97:
        out.append('<resize:%d,%d>' % random.choice([(1, 1), (40, 30), (3000, 2000), (200, 1500), (1500, 120), (900, 1100)]))
    else:
        out.append('<wait:%d>' % random.choice([10, 60, 600]))
s = ''.join(out)
# never close the last window: drop bare q (it would end the run early)
s = s.replace('q!', 'Q!').replace('q', 'Q')
print(s)
