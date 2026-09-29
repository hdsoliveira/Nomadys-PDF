"""Write tests/link.pdf: a line of text and one link to a web page, for the
window tests of tools/distro-test.sh (f follows it, y copies the text)."""
import sys

out = sys.argv[1] if len(sys.argv) > 1 else 'tests/link.pdf'
url = 'https://example.org/nomadys-test'
text = b'BT /F1 18 Tf 72 700 Td (Nomadys link test) Tj 0 -30 Td (Open the page) Tj ET'
objs = [
    b'<< /Type /Catalog /Pages 2 0 R >>',
    b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
    b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R '
    b'/Resources << /Font << /F1 5 0 R >> >> /Annots [6 0 R] >>',
    b'<< /Length %d >>\nstream\n' % len(text) + text + b'\nendstream',
    b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    b'<< /Type /Annot /Subtype /Link /Rect [72 665 200 685] /Border [0 0 0] '
    b'/A << /S /URI /URI (' + url.encode() + b') >> >>',
]
pdf = b'%PDF-1.4\n'
offsets = []
for i, body in enumerate(objs, 1):
    offsets.append(len(pdf))
    pdf += b'%d 0 obj\n' % i + body + b'\nendobj\n'
xref = len(pdf)
pdf += b'xref\n0 %d\n0000000000 65535 f \n' % (len(objs) + 1)
pdf += b''.join(b'%010d 00000 n \n' % o for o in offsets)
pdf += b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objs) + 1, xref)
open(out, 'wb').write(pdf)
