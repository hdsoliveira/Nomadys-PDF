import os, random, sys, zlib
os.makedirs(sys.argv[1], exist_ok=True); os.chdir(sys.argv[1])
random.seed(7)
def write_pdf(path, objs, root=1, trailer_extra='', bad_xref=False):
    data = bytearray(b'%PDF-1.7\n')
    off = {}
    for n in sorted(objs):
        off[n] = len(data)
        body = objs[n] if isinstance(objs[n], bytes) else objs[n].encode('latin-1')
        data += f'{n} 0 obj\n'.encode() + body + b'\nendobj\n'
    size = max(objs) + 1; x = len(data)
    data += f'xref\n0 {size}\n0000000000 65535 f \n'.encode()
    for n in range(1, size):
        o = off.get(n, 0) + (random.randint(5, 900) if bad_xref else 0)
        data += (f'{o:010d} 00000 n \n' if n in off else '0000000000 65535 f \n').encode()
    data += f'trailer\n<< /Size {size} /Root {root} 0 R {trailer_extra}>>\nstartxref\n{x}\n%%EOF\n'.encode()
    open(path, 'wb').write(data)
def stream(s):
    b = s.encode('latin-1'); return b'<< /Length %d >>\nstream\n' % len(b) + b + b'\nendstream'
font = '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'
def pages_pdf(path, boxes, rotates=None, text='Hello page', outline=None, bad_xref=False):
    objs = {1: '<< /Type /Catalog /Pages 2 0 R %s>>' % ('/Outlines 90 0 R ' if outline else ''), 3: font}
    kids = []
    for i, box in enumerate(boxes):
        pn, cn = 10 + 2 * i, 11 + 2 * i
        rot = f'/Rotate {rotates[i]} ' if rotates else ''
        x0, y0 = box[0] + 20, box[1] + 20
        objs[pn] = f'<< /Type /Page /Parent 2 0 R /MediaBox [{box[0]} {box[1]} {box[2]} {box[3]}] {rot}/Resources << /Font << /F1 3 0 R >> >> /Contents {cn} 0 R >>'
        objs[cn] = stream(f'BT /F1 12 Tf {x0} {y0} Td ({text} {i+1}) Tj ET')
        kids.append(f'{pn} 0 R')
    objs[2] = f'<< /Type /Pages /Kids [{" ".join(kids)}] /Count {len(kids)} >>'
    if outline:
        objs.update(outline)
    write_pdf(path, objs, bad_xref=bad_xref)
A4 = (0, 0, 595, 842)
pages_pdf('rotated.pdf', [A4, (-100, -100, 495, 742), A4, (50, 50, 900, 400)], rotates=[0, 90, 180, 270])
pages_pdf('huge_page.pdf', [(0, 0, 14400, 14400), A4])
pages_pdf('tiny_page.pdf', [(0, 0, 3, 3), (0, 0, 1, 800), A4])
pages_pdf('many_pages.pdf', [A4] * 3000)
pages_pdf('bad_xref.pdf', [A4] * 5, bad_xref=True)
# cyclic outline: 91 -> 92 -> 91
cyc = {90: '<< /Type /Outlines /First 91 0 R /Last 92 0 R /Count 2 >>',
       91: '<< /Title (Loop A) /Parent 90 0 R /Next 92 0 R /First 92 0 R /Dest [10 0 R /XYZ 0 800 0] >>',
       92: '<< /Title (Loop B) /Parent 90 0 R /Next 91 0 R /First 91 0 R /Dest [12 0 R /Fit] >>'}
pages_pdf('cyclic_outline.pdf', [A4, A4], outline=cyc)
write_pdf('zero_pages.pdf', {1: '<< /Type /Catalog /Pages 2 0 R >>', 2: '<< /Type /Pages /Kids [] /Count 0 >>'})
write_pdf('no_pages_tree.pdf', {1: '<< /Type /Catalog >>'})
good = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sample.pdf'), 'rb').read()
open('truncated.pdf', 'wb').write(good[:len(good) // 2])
open('garbage.pdf', 'wb').write(bytes(random.getrandbits(8) for _ in range(20000)))
open('empty.pdf', 'wb').write(b'')
open('text.pdf', 'w').write('this is not a pdf\n' * 50)
open('header_only.pdf', 'wb').write(b'%PDF-1.7\n%%EOF\n')
# form with many field types, some without appearance streams
objs = {1: '<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [20 0 R 21 0 R 22 0 R 23 0 R 24 0 R 25 0 R] /NeedAppearances true /DA (/Helv 10 Tf 0 g) /DR << /Font << /Helv 3 0 R >> >> >> >>',
        2: '<< /Type /Pages /Kids [10 0 R] /Count 1 >>', 3: font,
        10: '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R >> >> /Contents 11 0 R /Annots [20 0 R 21 0 R 22 0 R 23 0 R 24 0 R 25 0 R 26 0 R] >>',
        11: stream('BT /F1 12 Tf 50 800 Td (Many field types) Tj ET')}
w = '/Type /Annot /Subtype /Widget /P 10 0 R /F 4'
objs[20] = f'<< {w} /FT /Tx /T (multi) /Ff 4096 /Rect [50 600 300 700] /DA (/Helv 10 Tf 0 g) /V (line one) >>'
objs[21] = f'<< {w} /FT /Ch /T (combo) /Ff 131072 /Opt [(Alpha) (Beta) (Gamma)] /Rect [50 550 200 570] /DA (/Helv 10 Tf 0 g) >>'
objs[22] = f'<< {w} /FT /Ch /T (list) /Opt [(One) (Two) (Three)] /Rect [50 450 200 530] /DA (/Helv 10 Tf 0 g) >>'
objs[23] = f'<< {w} /FT /Btn /T (radio) /Ff 49152 /Rect [50 400 70 420] /AS /Off /MK << /CA (l) >> >>'
objs[24] = f'<< {w} /FT /Tx /T (readonly) /Ff 1 /Rect [50 350 300 370] /V (cannot edit) >>'
objs[25] = f'<< {w} /FT /Tx /T (maxlen) /MaxLen 5 /Rect [50 300 300 320] >>'
objs[26] = '<< /Type /Annot /Subtype /Text /Rect [400 700 420 720] /Contents (A sticky note) /P 10 0 R >>'
write_pdf('many_fields.pdf', objs)
print('ok')

# images for ":image" and I in the fuzzer: good, transparent, broken
import struct
def png(path, w, h, alpha=True, cut=None):
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    rows = b''.join(b'\0' + b''.join(bytes((x * 30 % 256, y * 40 % 256, 90) + ((x * 20 % 256,) if alpha else ()))
                                     for x in range(w)) for y in range(h))
    data = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6 if alpha else 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
    open(path, 'wb').write(data[:cut] if cut else data)
png('pic.png', 64, 40)
png('opaque.png', 30, 90, alpha=False)
png('trunc.png', 64, 40, cut=60)
open('bad.png', 'wb').write(bytes(random.randrange(256) for _ in range(500)))
try:
    from PIL import Image
    Image.new('RGB', (120, 80), (200, 30, 40)).save('pic.jpg', quality=80)
except ImportError:
    pass
