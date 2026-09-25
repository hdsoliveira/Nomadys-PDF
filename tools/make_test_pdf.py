"""Write tests/sample.pdf: text, a raster image, vector colours and form fields."""
import sys
import zlib

out = sys.argv[1] if len(sys.argv) > 1 else 'tests/sample.pdf'
objs = {}


def obj(n, body):
    objs[n] = body if isinstance(body, bytes) else body.encode('latin-1')


def stream(n, data, extra=''):
    obj(n, f'<< /Length {len(data)} {extra}>>\nstream\n'.encode() + data + b'\nendstream')


# 1 catalog, 2 pages, 3 font, 4 image, 5-7 pages, 8-10 contents, 11-15 fields, 16-17 checkbox APs
obj(1, '<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [11 0 R 12 0 R 13 0 R 14 0 R] '
       '/DA (/Helv 11 Tf 0 g) /DR << /Font << /Helv 3 0 R >> >> /NeedAppearances true >> >>')
obj(2, '<< /Type /Pages /Kids [5 0 R 6 0 R 7 0 R] /Count 3 >>')
obj(3, '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>')
w = h = 64
pix = bytearray()
for y in range(h):
    for x in range(w):
        pix += bytes((x * 4, y * 4, 255 - x * 2))
stream(4, zlib.compress(bytes(pix)), f'/Type /XObject /Subtype /Image /Width {w} /Height {h} '
       '/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode ')

res = '/Resources << /Font << /F1 3 0 R /Helv 3 0 R >> /XObject << /Im1 4 0 R >> >>'
obj(5, f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] {res} /Contents 8 0 R '
       '/Annots [11 0 R 12 0 R 13 0 R] >>')
obj(6, f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] {res} /Contents 9 0 R /Annots [14 0 R] >>')
obj(7, f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] {res} /Contents 10 0 R >>')


def text_lines(x, y, lines, size=12):
    s = f'BT /F1 {size} Tf {x} {y} Td {size * 1.4:.1f} TL\n'
    for line in lines:
        s += f'({line}) Tj T*\n'
    return s + 'ET\n'


lorem = ['Nomadys-PDF test document. The quick brown fox jumps over the lazy dog.',
         'Second line of text to test caret movement with j and k keys.',
         'Third line: search for the word zebra here, and elsewhere.',
         'Fourth line with words: alpha beta gamma delta epsilon.']
p1 = text_lines(56, 780, ['Page one'], 20) + text_lines(56, 740, lorem)
p1 += 'q 200 0 0 150 56 470 cm /Im1 Do Q\n'
p1 += '1 0 0 rg 300 470 120 60 re f 0 0.6 0 rg 300 550 120 60 re f 0 0 0 RG 2 w 440 470 m 540 610 l S\n'
p1 += text_lines(56, 420, ['Name:', '', 'City:', '', 'I agree:'])
stream(8, p1.encode('latin-1'))
stream(9, (text_lines(56, 780, ['Page two'], 20) + text_lines(56, 740, lorem[::-1] + ['zebra again'])
           + text_lines(56, 600, ['Comment:'])).encode('latin-1'))
stream(10, (text_lines(56, 780, ['Page three'], 20) + text_lines(56, 740, lorem * 3)).encode('latin-1'))

field = '/Type /Annot /Subtype /Widget /F 4 /MK << /BC [0.4 0.4 0.4] /BG [1 1 1] >> /Border [0 0 1]'
obj(11, f'<< {field} /FT /Tx /T (name) /Rect [140 408 400 430] /DA (/Helv 11 Tf 0 g) /P 5 0 R >>')
obj(12, f'<< {field} /FT /Tx /T (city) /Rect [140 380 400 402] /DA (/Helv 11 Tf 0 g) /P 5 0 R >>')
obj(13, f'<< {field} /FT /Btn /T (agree) /Rect [140 352 158 370] /V /Off /AS /Off /P 5 0 R '
        '/DA (/ZaDb 12 Tf 0 g) /AP << /N << /On 16 0 R /Off 17 0 R >> >> >>')
obj(14, f'<< {field} /FT /Tx /T (comment) /Rect [140 588 500 610] /DA (/Helv 11 Tf 0 g) /P 6 0 R >>')
stream(16, b'0 0 0 rg 3 3 12 12 re f', '/Type /XObject /Subtype /Form /BBox [0 0 18 18] ')
stream(17, b'', '/Type /XObject /Subtype /Form /BBox [0 0 18 18] ')

data = bytearray(b'%PDF-1.7\n%\xe2\xe3\xcf\xd3\n')
offsets = {}
for n in sorted(objs):
    offsets[n] = len(data)
    data += f'{n} 0 obj\n'.encode() + objs[n] + b'\nendobj\n'
size = max(objs) + 1
xref = len(data)
data += f'xref\n0 {size}\n0000000000 65535 f \n'.encode()
for n in range(1, size):
    data += (f'{offsets[n]:010d} 00000 n \n' if n in offsets else '0000000000 65535 f \n').encode()
data += f'trailer\n<< /Size {size} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
open(out, 'wb').write(data)
print(out, len(data), 'bytes')
