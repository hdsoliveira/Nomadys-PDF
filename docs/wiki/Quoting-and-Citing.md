# Quoting and citing

## Copying text

In the text cursor (`v`), select with `v`, then:

| Key | Copies |
|---|---|
| `y` | the text, as it is |
| `Y` | the text **as a quotation with its page**: `“the snap-through instability” (p. 5)` |

Quotations are made for pasting into your own writing:

- line breaks and repeated spaces become single spaces, and words broken
  with a hyphen at the end of a line are joined again;
- the page is the one **printed** on it (its page label, such as `iv` or
  `S3`, or `215` for page 215 of a journal volume), not the position in the
  file, whenever the PDF says so;
- a selection over two pages gives `(pp. 5–6)`.

The format is `cite_format` in the [configuration](Configuration.md), with
`%t` for the text and `%p` for the page(s). The default is
`“%t” (p. %p)`; for example:

```ini
cite_format = "%t" [p. %p]
cite_format = > %t (p. %p)
```

## BibTeX for the paper you are reading

`:bibtex` copies the paper's BibTeX entry to the clipboard, ready to paste
into your `.bib` file:

1. it finds the paper's **DOI**: in the PDF's metadata first, then in the
   text of its first two pages (a DOI printed as `doi:`, `DOI:` or
   `doi.org/` is preferred over one that is only mentioned);
2. without a DOI, it looks for an **arXiv** number (`arXiv:1602.03837`);
3. it fetches the entry from `doi.org` (or `arxiv.org`) in the background,
   so you keep reading meanwhile;
4. the message shows the citation key: "BibTeX copied: Abbott_2016".

It needs `curl` and an internet connection. When nothing is found it says
so: "no DOI or arXiv number found on the first pages", "no connection",
"timed out" or "not found".

## Copying a figure

To paste a figure into slides or a document as an image, see
[Images and regions](Images-and-Regions.md#copying-a-region-as-an-image).
