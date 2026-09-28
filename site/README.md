# The Nomadys-PDF website

A static site: plain HTML and CSS, no framework, no JavaScript, nothing
loaded from other servers (no web fonts, no trackers), so it can be put on
any web host as it is.

| Path | What |
|---|---|
| `index.html` | the front page (written by hand) |
| `docs/` | the documentation, **generated** from `docs/wiki/*.md` by `build.py` |
| `assets/style.css` | the look of every page (light and dark, following the system) |
| `assets/demos/` | the demo videos (MP4 for the site, GIF for sharing, JPG stills), **generated** by `demos/make_demos.py` |
| `demos/` | the recorder of the demos and the fictional paper they show |

## Building

```sh
python3 site/build.py                       # the documentation pages, after editing docs/wiki
python3 site/demos/make_demos.py            # every demo video (needs make, pdflatex, mutool, ffmpeg, Pillow)
python3 site/demos/make_demos.py library    # only one
```

The demos are recorded with npdf itself, in its headless test mode
(`SDL_VIDEODRIVER=offscreen`, `--test-script`), so they always show the
current version. Each demo is a short list of keys in `make_demos.py`; the
keys pressed are drawn in the corner of the video. The PDFs shown are
fictional papers made for this (`demos/paper/paper.tex` and the small
library in `make_demos.py`), so no real paper appears on the site.

To look at the site locally:

```sh
cd site && python3 -m http.server 8000     # then http://localhost:8000
```

## Publishing

The `site/` folder is the whole website. Any of these works:

- **GitHub Pages**: Pages publishes only the root or `/docs` of a branch,
  so push the `site/` folder to its own branch and publish that branch:
  `git subtree push --prefix site origin gh-pages`, then Settings → Pages →
  branch `gh-pages`, folder `/`. On a private repository Pages needs a paid
  GitHub plan; otherwise use one of the hosts below.
- **Codeberg Pages**, **Netlify**, **Cloudflare Pages**: point them at the
  `site/` folder; there is no build step.
- **Any web server**: copy the folder, e.g.
  `rsync -a site/ user@server:/var/www/nomadys-pdf/`.
