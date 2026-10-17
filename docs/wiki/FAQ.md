# FAQ and troubleshooting

**Where are all the buttons?**
There are none, on purpose, because the page gets the whole window. The leader
key `\` starts many functions, and `\c` opens a menu that lists every key. `:`
and `Tab` list every command.

**How do I get out of whatever I am in?**
`Esc`, once or twice, always goes back to reading.

**Will other programs see my highlights and comments?**
Yes. They are standard PDF annotations, stored in the file itself, so Acrobat,
Okular, Evince, Preview, Zotero and others show them. Equations in comments
appear there as the LaTeX you typed (`$\sigma = E\varepsilon$`).

**I deleted a highlight by mistake.**
`u` brings it back (until the file is closed).

**My highlights disappeared after LaTeX recompiled the PDF.**
LaTeX writes a completely new file, so annotations made in the old one are
gone. Annotate the final version, or a copy of it. Marks survive, since
they are kept outside the PDF.

**`\/` finds nothing.**
The first time, the index is still being built, and the top right corner shows
the progress. Check that `pdf_dirs` in the configuration includes the
folders with your PDFs. Scanned PDFs without a text layer and
password-protected PDFs cannot be searched. Letters with accents must be
typed with the accent.

**`\f` or `\/` does not see my cloud folder.**
Folders on network drives and FUSE mounts (rclone, sshfs, some cloud
clients) are skipped on purpose, since reading them could download every
file. Folders synchronised to the local disk (Nextcloud, Dropbox, OneDrive
clients) are fine.

**The index takes space.**
About 9 KB per PDF. `~/.cache/nomadys-pdf/index2` can be deleted at any time;
limit `pdf_dirs` to the folders you care about to keep it small.

**Equations in comments show as plain text.**
They need `pdflatex` (TeX Live) with `amsmath` and `amssymb`. Errors appear
under the comment.

**`:bibtex` says "no DOI or arXiv number found".**
The paper does not print its DOI on the first two pages, nor in its
metadata. It needs `curl` and an internet connection.

**The interface font is wrong.**
Set `font = ` to a name from `fc-list : family` (for example
`font = JetBrains Mono`). An unknown name is reported when the program
starts.

**Dark mode makes a figure look wrong.**
Photos and the pictures you placed (`Ctrl-v` then `i`, `:image`) keep their
colours, and drawings on white are recoloured; if one is judged wrongly,
`dark_images = keep` leaves every image untouched (or switch with `i` for
that figure).

**Form calculations do not update.**
Nomadys-PDF does not run JavaScript inside PDFs, which some forms use for
automatic calculations.

**Can I undo deleting or moving pages?**
Yes, with `u`, until the file is closed. The pages of signed PDFs cannot
be changed.

**How do I make it the default PDF reader?**
`xdg-mime default npdf.desktop application/pdf`

**How do I open the PDFs of neomutt (or another program) in tabs of one window?**
Use `npdf --tab file.pdf`: if npdf is running, the file opens as a new tab
there (or switches to the tab it already has); if not, npdf starts as usual. It
returns only once the file is open, so the program that made a temporary file
can delete it afterwards. For neomutt, in `~/.mailcap`:

    application/pdf; npdf --tab %s; test=test -n "$WAYLAND_DISPLAY" -o -n "$DISPLAY"

If the first PDF opened this way is the one that starts npdf, neomutt waits
until you close that window; open the next ones from neomutt as usual and
they become tabs.

**How do I report a bug or reach the author?**
`\a` shows the contact address; the source and issues are on
[GitHub](https://github.com/hdsoliveira/Nomadys-PDF).
