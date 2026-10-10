# Security

Nomadys-PDF opens files that other people made, so it treats every PDF, and every
file next to one, as hostile. What it does about that is listed under "Safety" in
the [changelog](CHANGELOG.md), and `tests/qa/security.py` checks it.

## Reporting a problem

Please write to nomadys-pdf@mailbox.org, or open a private security advisory on
GitHub, before making a problem public. A PDF that reproduces it helps most.
You will get an answer within a few days.

## Keeping safe

- Update the program when a new version comes out. The PDF engine (PDFium) is
  bundled and is updated with it.
- Keep a copy of PDFs that matter. The program keeps the file as it was in
  `name.pdf.bak` before the first save, but a copy of your own is safer.
- Autosave writes into the open file; set `autosave = false` to leave an
  original untouched.
