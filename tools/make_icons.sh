#!/bin/sh
# Renders docs/icon.svg to icons/*.png and embeds the 256 px one in the
# program (src/icon_png.h). Needs inkscape; the results are in git, so
# building does not.
set -e
cd "$(dirname "$0")/.."
for s in 16 24 32 48 64 128 256 512; do
    inkscape docs/icon.svg --export-type=png --export-filename=icons/nomadys-pdf-$s.png -w $s -h $s >/dev/null 2>&1
done
python3 - <<'PY'
data = open('icons/nomadys-pdf-256.png', 'rb').read()
rows = [', '.join('0x%02x' % b for b in data[i:i + 16]) for i in range(0, len(data), 16)]
open('src/icon_png.h', 'w').write(
    '/* The program\'s icon, 256 px PNG (tools/make_icons.sh renders docs/icon.svg). */\n'
    'static const unsigned char icon_png[] = {\n    ' + ',\n    '.join(rows) + ',\n};\n')
PY
echo "icons/ and src/icon_png.h made"
