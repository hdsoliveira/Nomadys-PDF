# Searching

There are four kinds of search, from the narrowest to the widest.

| Key | Searches |
|---|---|
| `/` | the text of this PDF |
| `\o` | the names of the files you opened recently |
| `\f` | the names of every PDF in your folders |
| `\/` and `\n` | the **text** of every PDF in your folders, and every **comment and highlight** in them |

And there is one comparison, `:diff`, which shows what changed between two versions of a
paper.

## In this PDF, with `/`

Type `/` and a word. The search is **incremental**, so while you type, the view
goes to the first match after where you were.

| Key | Action |
|---|---|
| `Enter` | keep that match (it is a jump, and `Ctrl-o` returns) |
| `Esc` in the prompt | cancel, back where you were |
| `Ctrl-t` in the prompt | match upper and lower case exactly (the prompt shows `/Aa`) |
| `n` / `N` | next / previous match, across pages |
| `\s` (or `:hits words`) | every match as a list with the words around it; `j` `k` move, `Enter` goes to it, `Esc` closes |
| `Esc`, `:noh` | remove the highlighting |

Every match on screen is highlighted, and the message says which one you are
at ("match 3 of 17", for documents up to 150 pages). The search is
**smart case**, so a query with a capital letter matches case exactly,
and otherwise it does not.

## Recent files, with `\o`

`\o` (or `:recent`) lists the files you opened, most recent first. Type
parts of the name to filter.

## Every PDF on your computer, with `\f`

`\f` (or `:find`) lists every PDF in the folders of `pdf_dirs` (your home
folder by default), newest first, and filters them as you type. Hidden
folders (`.cache`, `.local`, …), folders of programs (`node_modules`,
`site-packages`, `build`, …) and network or cloud mounts are skipped. The
whole home folder is scanned in about a second, in the background; the list
of the previous scan shows at once.

**Typing** in both lists is *fuzzy*, so the letters of a word must appear in
order, not necessarily together (`bstshl` finds `bistable-shells.pdf`).
Several words separated by spaces may be in any order and may match the
folders too (`2024 shells`); a word found in the file name counts more than
one found only in a folder name.

| Key | Action |
|---|---|
| typing, `Backspace` | filter |
| `Ctrl-u` | clear |
| `Ctrl-j` `Ctrl-k`, `Ctrl-n` `Ctrl-p`, `↓` `↑`, `Tab` `Shift-Tab` | move |
| `Enter` | open (or go to its tab, if already open) |
| `Esc` | close |

## Inside every PDF, with `\/` and `\n`

`\/` (or `:library words`) searches the **text** of every PDF in `pdf_dirs`.
`\n` (or `:comments words`) searches every **comment and highlighted text**
in them. `Tab` switches between the two.

- The words may be in any order but must all be **on the same page**;
  pages where they appear together as you typed them come first, then the
  newest files.
- Upper and lower case do not matter (for letters without accents; `é`
  must be typed as `é`).
- Each result shows the file, the page and the passage, with the words
  coloured. In `\n` it also shows the highlight's colour, the comment and
  the highlighted text.
- With nothing typed, `\n` lists **every comment** you ever wrote, newest
  files first.
- `Ctrl-t` (or `:tasks`) keeps only the **tasks still to do**, from every
  PDF, which is your to-do list across all your papers (see
  [Highlights and comments](Highlights-and-Comments.md#tasks-ticking-your-highlights-off)).
- `Enter` opens the PDF at that page with the words highlighted; `n` and
  `N` go on to the next matches in that PDF.

| Key | Action |
|---|---|
| typing, `Backspace` | search (each letter refines it at once) |
| `Tab` | text ⇄ comments and highlights |
| `Ctrl-t` | comments ⇄ the tasks to do |
| `Ctrl-j` `Ctrl-k`, `↓` `↑`, `Ctrl-d` | move |
| `Ctrl-u` | clear |
| `Enter` | open at that page |
| `Esc` | close |

### The index

To make this fast, the text of your PDFs is kept in an **index**. It is made
the first time you press `\/` or `\n`, by three background processes at low
priority, so you can keep reading (and searching what is already indexed)
meanwhile; the top right corner shows the progress ("indexing 1234 / 14195").

- The first time takes about 16 ms per PDF (about 4 minutes for 14 000
  PDFs). Afterwards only new or changed files are read again, which takes
  a fraction of a second.
- It lives in `~/.cache/nomadys-pdf/index2`, about 9 KB per PDF
  (compressed). Deleting that folder is safe, because it is made again. (The index
  of versions before tasks, `index`, is removed and made again once.)
- A broken PDF cannot crash the reader, because the index is made by separate
  processes, and a PDF that crashes one is skipped until it changes.
- PDFs with a password and scanned PDFs without a text layer have no text to
  index.
- Folders on network drives or cloud mounts (FUSE, NFS, SMB) are skipped,
  since reading them could download every file.

A search over 2 700 PDFs answers in under 0.2 s.

## Comparing two versions with `:diff`

`:diff old-version.pdf` compares the PDF on screen with an older version,
word by word.

- new text is shaded **green**
- changed text is shaded **amber**
- a **red** mark shows where text was deleted

`]c` / `[c` go to the next / previous change, and the message says what the
old text was, such as "change 3 of 9: changed, was “0.47”". `:nodiff` hides it.
Nothing is written into the PDF.

**The notes are compared too**, including highlights, comments (and their replies),
text boxes, pictures and drawings. A frame is drawn round each one that is
**new** (green), **changed** (amber, because its comment or text is different) or
**removed** (red, where it was), and `]c` takes you through them together with
the changes in the text, with messages such as "new highlight “check this value”" and "text box
changed, was “Hello”, now “Hello world”". A note is the same one in both
versions when it has the same name inside the PDF or, without one, the same
kind in the same place. If nothing differs the message says "no change in the
text or the notes".

Figures and anything that is not text or a note are *not* compared.

The comparison ignores what is not a real change, such as words hyphenated at the
end of a line, ligatures (ﬁ, ﬂ) and page numbers, so text that only moved to
another page does not show up. When LaTeX recompiles the PDF, the comparison
is made again automatically, which is handy when answering reviewers.
