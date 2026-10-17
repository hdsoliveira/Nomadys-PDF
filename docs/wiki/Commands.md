# Commands

Type `:` and the command. `Tab` completes command names and file paths;
with several options they are listed above the line and `Tab` /
`Shift-Tab` cycle through them. `:` followed by `Tab` lists every command.
`↑` and `↓` bring back what you typed before (the last 100 commands, kept
between runs); the same for `/` searches.

## Files and tabs

| Command | Action |
|---|---|
| `:w` | save |
| `:w name.pdf` | save a **copy** under that name, next to this PDF (a folder in the name works; `Tab` completes). It refuses a name that is already there; `:w! name.pdf` replaces it. The PDF you are reading stays the open one |
| `:q` | close the tab |
| `:q!` | close it, discarding unsaved changes |
| `:wq`, `:x` | save and close the tab |
| `:e file.pdf` | open a PDF in a new tab |
| `:reload` | read the PDF again from disk |
| `:recent` | recent files (`\o`) |
| `:find` | find any PDF in your folders (`\f`) |
| `:session` | reopen the tabs of the last session |
| `:print [pages] [xN] [@printer] [duplex]` | print, e.g. `:print 3-7,10 x2 @office duplex` |

## Moving and viewing

| Command | Action |
|---|---|
| `:42`, `:page 42` | go to page 42 |
| `:zoom 150` | zoom in percent |
| `:rotate 90`, `:rotate -90`, `:rotate 180` | turn the view |
| `:dark` / `:light` | dark / light mode |
| `:pages` | page grid (`\g`) |
| `:present` | presentation (`F5`) |
| `:marks`, `:notes` | the panel (`\l`) |
| `:noh` | remove the search highlighting |

## Split view

| Command | Action |
|---|---|
| `:vsplit [file.pdf]` | split side by side, this PDF or another |
| `:split [file.pdf]` | split one above the other |
| `:close` | close this pane |
| `:only` | keep only this pane |

## Searching and comparing

| Command | Action |
|---|---|
| `:library words` | search the text of all your PDFs (`\/`) |
| `:comments [words]` | search the comments and highlights of all your PDFs (`\n`) |
| `:tasks [words]` | the tasks still to do in all your PDFs |
| `:diff old.pdf` | compare with an older version, text and notes (`]c` `[c` go through the changes) |
| `:nodiff` | hide the comparison |

## Notes, quotes and references

| Command | Action |
|---|---|
| `:export [file.md] [colours] [commented] [tasks]` | highlights, comments and marks to Markdown (tasks as `- [ ]` / `- [x]`) |
| `:export! …` | the same, allowed to overwrite a file it did not write |
| `:response [file.md]` | the response letter to the reviewers, with their comments and your replies (`r`), by reviewer ([more](Highlights-and-Comments.md#answering-the-reviewers)) |
| `:response! …` | the same, allowed to overwrite a file it did not write |
| `:merge copy.pdf` | bring in a co-author's highlights and comments |
| `:bibtex` | copy the paper's BibTeX entry (by its DOI or arXiv number) |
| `:image file.png` | put a picture on the page |

## Pages

| Command | Action |
|---|---|
| `:extract 3-7 [file.pdf]` | pages 3–7 as a new PDF |
| `:delete 3-7` | delete pages (`u` undoes it) |
| `:insert other.pdf` | insert another PDF after this page |
| `:append other.pdf` | add another PDF at the end |

Page ranges can be `5`, `3-7` or `10-` (to the end).

## Look and information

| Command | Action |
|---|---|
| `:theme` | choose a theme (`\T`) |
| `:theme NAME` | switch to a theme |
| `:theme new NAME` | make your own theme from the current one |
| `:about` | version, author, contact (`\a`) |
| `:help` | every key (`\c`) |

## From the terminal

| Command | Action |
|---|---|
| `npdf file.pdf [more.pdf …]` | open files, each in a tab |
| `npdf` | reopen the last session |
| `npdf --export file.pdf` | print its highlights and comments as Markdown |
| `npdf --synctex-forward LINE:COL:FILE.tex file.pdf` | show that source line (used by VimTeX) |
| `npdf -x "COMMAND" file.pdf` | the editor command for inverse search (`%l` `%c` `%f`) |
| `npdf --version`, `npdf --help` | version, short help |
