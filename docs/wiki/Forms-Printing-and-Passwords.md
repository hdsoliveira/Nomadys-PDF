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

## Saved forms

Fill a form once, keep the answers under a name (say `P80`), and fill every
blank of the next one at once.

| Key | Action |
|---|---|
| `\F` | the menu of saved forms |
| `Enter` (in the menu) | fill this PDF with the chosen one |
| `n` (in the menu) | save what is filled in now, asking for a name |
| `x` (in the menu) | delete the chosen one (asks first) |
| `:form save P80` | save what is filled in now as `P80` |
| `:form P80` | fill with `P80` |
| `:form delete P80` | delete `P80` |

Answers are kept by field name, in the `forms` file of the data folder, so a
form works on any PDF whose fields are named the same; fields the PDF does not
have are reported. Text, multi-line text, combo and list boxes, ticked
checkboxes and chosen radio buttons are kept. A list box keeps one option.
Filling is not covered by `u`; `Ctrl-z` undoes a field.

## Printing

`Ctrl-p` opens the print line (`Enter` prints everything to the default
printer), and `:print` takes options in any order.

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

A protected PDF asks for its password, either in the window (the letters are
hidden) or, when started from a terminal, in the terminal. The password is
kept in memory only, to reload and save the file (which stays encrypted),
and wiped when the PDF is closed. It is never written anywhere.

## Signed PDFs

Digitally signed PDFs keep their signature valid, because changes (highlights, form
entries) are appended to the file instead of rewriting it, as the
signature requires. Page changes are refused for them.
