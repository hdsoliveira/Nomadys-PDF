#!/usr/bin/env python3
"""Builds the documentation pages of the website from docs/wiki/*.md.

    python3 site/build.py

Writes site/docs/*.html (Home.md becomes site/docs/index.html) and copies the
icon and the theme picture into site/assets. site/index.html (the front
page) is written by hand. No dependencies: a small Markdown converter for
what the wiki uses (headings, paragraphs, lists, tables, code, links,
images, bold, italics, inline HTML).
"""
import html
import re
import shutil
from pathlib import Path

SITE = Path(__file__).resolve().parent
ROOT = SITE.parent
WIKI = ROOT / "docs" / "wiki"
OUT = SITE / "docs"
VERSION = re.search(r'NOMADYS_VERSION "([^"]+)"', (ROOT / "src" / "nomadys.h").read_text()).group(1)

# the demo videos shown in a page: page -> [(heading slug prefix or None for the top, demo)]
DEMOS = {
    "Reading-and-Navigation": [(None, "reading", "Scrolling, zoom and the smart dark mode"),
                               ("links-references-and-previews", "preview", "Previewing a citation, then jumping to it")],
    "Searching": [("inside-every-pdf", "library", "Searching the text, then the comments, of every PDF"),
                  ("comparing-two-versions", "diff", "Comparing with the submitted version")],
    "Highlights-and-Comments": [(None, "highlight", "Highlighting, and a comment with an equation")],
    "Quoting-and-Citing": [(None, "quote", "A quotation with its page, ready to paste")],
    "Changing-Pages": [(None, "pages", "Choosing pages, moving, deleting, undoing")],
    "Tabs-Windows-and-Split-View": [("split-view", "split", "Two panes, then another paper")],
    "Themes-and-Dark-Mode": [("choosing-one", "themes", "Trying themes live")],
}


def slug(text):
    text = re.sub(r"<[^>]+>", "", text).lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.strip().replace(" ", "-")


def link_target(url):
    if url.startswith(("http://", "https://", "mailto:", "#")):
        return url
    url = url.replace("../icon.svg", "../assets/icon.svg").replace("../themes.png", "../assets/themes.png")
    m = re.match(r"([\w\-]+)\.md(#.*)?$", url)
    if m:
        page = "index" if m.group(1) == "Home" else m.group(1)
        return f"{page}.html{m.group(2) or ''}"
    return url


def inline(text):
    """Code spans, then images, links, bold and italics in the rest."""
    parts = re.split(r"(`[^`]+`)", text)
    out = []
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 1:
            out.append("<code>" + html.escape(p[1:-1]) + "</code>")
            continue
        if re.match(r"\s*<(img|br|kbd)\b", p):  # inline HTML the wiki uses
            out.append(re.sub(r'src="([^"]+)"', lambda m: f'src="{link_target(m.group(1))}"', p))
            continue
        p = html.escape(p, quote=False)
        p = re.sub(r"!\[([^\]]*)\]\(([^)]+)\)", lambda m: f'<img alt="{m.group(1)}" src="{link_target(m.group(2))}">', p)
        p = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", lambda m: f'<a href="{link_target(m.group(2))}">{m.group(1)}</a>', p)
        p = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", p)
        p = re.sub(r"(?<![\w*])\*([^*\s][^*]*?)\*(?![\w*])", r"<em>\1</em>", p)
        out.append(p)
    return "".join(out)


def cells(line):
    """The cells of a table row; a | inside `code` does not split."""
    line = line.strip().strip("|")
    out, cur, code = [], "", False
    for ch in line:
        if ch == "`":
            code = not code
        if ch == "|" and not code:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    out.append(cur.strip())
    return out


def markdown(md):
    lines = md.split("\n")
    out, para, i = [], [], 0
    headings = []

    def flush():
        if para:
            out.append("<p>" + inline(" ".join(para)) + "</p>")
            para.clear()

    while i < len(lines):
        line = lines[i]
        s = line.strip()
        if s.startswith("```"):
            flush()
            code = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code.append(lines[i])
                i += 1
            out.append("<pre><code>" + html.escape("\n".join(code)) + "</code></pre>")
            i += 1
            continue
        m = re.match(r"(#{1,6}) (.*)", s)
        if m:
            flush()
            n, text = len(m.group(1)), inline(m.group(2))
            sid = slug(m.group(2))
            headings.append((n, sid))
            anchor = f'<a class="anchor" href="#{sid}">#</a>' if n > 1 else ""
            out.append(f'<h{n} id="{sid}">{text}{anchor}</h{n}>')
            i += 1
            continue
        if s.startswith("|") and i + 1 < len(lines) and re.match(r"\|?\s*:?-{3,}", lines[i + 1].strip()):
            flush()
            head = cells(s)
            rows = []
            i += 2
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append(cells(lines[i]))
                i += 1
            t = ["<table><thead><tr>" + "".join(f"<th>{inline(c)}</th>" for c in head) + "</tr></thead><tbody>"]
            for r in rows:
                t.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>")
            out.append("".join(t) + "</tbody></table>")
            continue
        m = re.match(r"(\s*)(-|\d+\.) (.*)", line)
        if m:
            flush()
            ordered = m.group(2) != "-"
            items = []
            while i < len(lines):
                m = re.match(r"(\s*)(-|\d+\.) (.*)", lines[i])
                if m:
                    items.append(m.group(3))
                elif lines[i].startswith("  ") and lines[i].strip() and items:
                    items[-1] += " " + lines[i].strip()  # the item goes on
                else:
                    break
                i += 1
            tag = "ol" if ordered else "ul"
            out.append(f"<{tag}>" + "".join(f"<li>{inline(x)}</li>" for x in items) + f"</{tag}>")
            continue
        if s.startswith("<") and not s.startswith("<kbd"):
            flush()
            out.append(inline(s))
            i += 1
            continue
        if not s:
            flush()
        else:
            para.append(s)
        i += 1
    flush()
    return "\n".join(out), headings


def demo_block(name, caption, prefix="../"):
    return (f'<figure class="demo"><div class="window"><div class="bar"><i></i><i></i><i></i><span>{html.escape(caption)}</span></div>'
            f'<video autoplay muted loop playsinline preload="metadata" poster="{prefix}assets/demos/{name}.jpg">'
            f'<source src="{prefix}assets/demos/{name}.mp4" type="video/mp4"></video></div></figure>')


def add_demos(page, body):
    for where, name, caption in DEMOS.get(page, []):
        block = demo_block(name, caption)
        if where is None:  # after the first paragraph
            body = body.replace("</p>", "</p>\n" + block, 1)
        else:
            m = re.search(rf'<h[23] id="{re.escape(where)}[^"]*">.*?</h[23]>', body)
            if m:
                body = body[:m.end()] + "\n" + block + body[m.end():]
    return body


def sidebar():
    groups, order = [], []
    for line in (WIKI / "_Sidebar.md").read_text().splitlines():
        s = line.strip()
        m = re.match(r"\*\*\[([^\]]+)\]\(([^)]+)\)\*\*", s)
        if m:
            groups.append((None, [(m.group(1), m.group(2)[:-3])]))
            order.append((m.group(1), m.group(2)[:-3]))
            continue
        m = re.match(r"\*\*(.+)\*\*", s)
        if m:
            groups.append((m.group(1), []))
            continue
        m = re.match(r"- \[([^\]]+)\]\(([^)]+)\)", s)
        if m and groups:
            groups[-1][1].append((m.group(1), m.group(2)[:-3]))
            order.append((m.group(1), m.group(2)[:-3]))
    return groups, order


def href(page):
    return "index.html" if page == "Home" else f"{page}.html"


HEADER = """<header class="top"><div class="wrap">
<a class="brand" href="{root}index.html"><img src="{root}assets/icon.svg" alt="">Nomadys-PDF</a>
<nav><a href="{root}index.html#features">Features</a><a href="{root}index.html#research">For research</a>
<a class="docs-link" href="{root}docs/index.html">Documentation</a><a href="{root}index.html#install">Install</a>
<a class="gh" href="https://github.com/hdsoliveira/Nomadys-PDF">GitHub</a></nav>
</div></header>"""

FOOTER = """<footer><div class="wrap">
<span>Nomadys-PDF {version} · MIT licence · made by Hugo Oliveira</span>
<span class="right"><a href="{root}docs/index.html">Documentation</a><a href="https://github.com/hdsoliveira/Nomadys-PDF">Source</a><a href="mailto:hs.oliveira@mailbox.org">Contact</a></span>
</div></footer>"""


def page_html(title, desc, body, side, pager):
    return f"""<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)} · Nomadys-PDF documentation</title>
<meta name="description" content="{html.escape(desc)}">
<link rel="icon" href="../assets/icon.svg"><link rel="stylesheet" href="../assets/style.css">
</head><body>
{HEADER.format(root="../")}
<div class="wrap docs">
<aside class="side">{side}</aside>
<main class="doc">
{body}
{pager}
</main>
</div>
{FOOTER.format(root="../", version=VERSION)}
</body></html>
"""


def main():
    OUT.mkdir(exist_ok=True)
    (SITE / "assets").mkdir(exist_ok=True)
    shutil.copy(ROOT / "docs" / "icon.svg", SITE / "assets" / "icon.svg")
    shutil.copy(ROOT / "docs" / "themes.png", SITE / "assets" / "themes.png")
    groups, order = sidebar()
    for md in sorted(WIKI.glob("*.md")):
        page = md.stem
        if page.startswith("_"):
            continue
        body, _ = markdown(md.read_text())
        body = add_demos(page, body)
        side = []
        for title, items in groups:
            if title:
                side.append(f"<p>{html.escape(title)}</p>")
            side.append("<ul>" + "".join(
                f'<li><a href="{href(p)}"{" class=on" if p == page else ""}>{html.escape(t)}</a></li>'
                for t, p in items) + "</ul>")
        names = [p for _, p in order]
        pager = ""
        if page in names:
            k = names.index(page)
            prev = order[k - 1] if k > 0 else None
            nxt = order[k + 1] if k + 1 < len(order) else None
            pager = '<nav class="pager">' + (
                f'<a href="{href(prev[1])}"><small>Previous</small>{html.escape(prev[0])}</a>' if prev else "<span></span>") + (
                f'<a class="next" href="{href(nxt[1])}"><small>Next</small>{html.escape(nxt[0])}</a>' if nxt else "") + "</nav>"
        title = re.search(r"^# (.+)$", md.read_text(), re.M).group(1)
        first = re.search(r"<p>(.*?)</p>", body, re.S)
        desc = re.sub(r"<[^>]+>", "", first.group(1))[:160] if first else title
        (OUT / href(page)).write_text(page_html(title, desc, body, "".join(side), pager))
    print(f"{len(list(OUT.glob('*.html')))} documentation pages in {OUT}")


if __name__ == "__main__":
    main()
