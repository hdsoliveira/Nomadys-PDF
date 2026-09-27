# Forms, printing and passwords

## Filling in forms

| Key | Action |
|---|---|
| `Ctrl-j` / `Ctrl-k` (or `Tab`) | next / previous field; the page follows |
| click | put the cursor in a field |
| typing | fills the field |
| `Space` | ticks / unticks a checkbox |
| `Ctrl-a`, `Ctrl-z`, `Ctrl-v` | select all, undo, paste |
| `Esc` | leave the field |

The badge shows `INSERT` while you type in a field. Entries are saved
automatically, like highlights.

PDF forms sometimes contain JavaScript (for example to add up fields);
Nomadys-PDF does not run it, so such automatic calculations do not happen.

## Printing

`Ctrl-p` opens the print line (`Enter` prints everything to the default
printer); `:print` takes options in any order:

| Option | Meaning |
|---|---|
| `3-7,10` | pages |
| `x2` | copies |
| `@name` | printer (`Tab` lists your printers) |
| `duplex` | both sides |

```
:print 1-4 x2 @office duplex
```

Printing goes through CUPS (`lp`). Unsaved highlights and form entries are
saved first, so they are printed too.

## Password-protected PDFs

A protected PDF asks for its password: in the window (the letters are
hidden) or, when started from a terminal, in the terminal. The password is
kept in memory only, to reload and save the file (which stays encrypted),
and wiped when the PDF is closed. It is never written anywhere.

## Signed PDFs

Digitally signed PDFs keep their signature valid: changes (highlights, form
entries) are appended to the file instead of rewriting it, as the
signature requires. Page changes are refused for them.
