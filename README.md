# Talks

Slides from my talks and workshops, published at **https://arnavballincode.github.io/Talks/**.

| Talk | Slides |
|---|---|
| Let the Agent Cook 🔥 · Building AI-native developer workflows (workshop, [hands-on repo](https://github.com/ArnavBallinCode/let-the-agent-cook)) | [/LetTheAgentCook/](https://arnavballincode.github.io/Talks/LetTheAgentCook/) |
| Open Tech Summit 2026 · VoxBento | [/OpenTechSummit2026/](https://arnavballincode.github.io/Talks/OpenTechSummit2026/) |

## Adding a talk

1. Create a folder at the top level, for example `OpenTechSummit2026Workshop/`.
2. Put the slides in it as `index.html`. Images, scripts and other files can sit beside it.
3. Push to `main`.

The workflow in `.github/workflows/pages.yml` publishes every top-level folder that has an
`index.html` and lists it on the landing page, using the page's `<title>` and
`<meta name="description">`. Anything whose name starts with `_` or `.` (such as `_build/`) stays
in the repository but isn't published.

Preview the site locally:

```bash
python .github/scripts/build_site.py && python -m http.server -d _site
```

## VoxBento deck controls

Arrow keys or space to move, **N** for speaker notes, **F** for full screen.
The deck's sources are in `OpenTechSummit2026/_build/`. Rebuild with:

```bash
uv run --no-project --with qrcode==7.4.2 python OpenTechSummit2026/_build/build.py OpenTechSummit2026/_build /tmp/deck.html
```

The rebuilt file is the body of the page; `OpenTechSummit2026/index.html` adds the
`<!doctype html>`, charset, viewport and description tags around it.

## Let the Agent Cook deck

Arrow keys, space or a clicker to move (each press also steps through builds and code highlights),
**N** speaker notes, **P** presenter window with timer and pacing, **O** all slides, **F** full
screen, **B** black screen, a number then **Enter** to jump. On phones it becomes a scrolling page.

Edit the sources in `LetTheAgentCook/_build/` (slides and notes are in `slides.html`), then rebuild
the complete page:

```bash
uv run --no-project --with qrcode==7.4.2 python LetTheAgentCook/_build/build.py
```

This writes `LetTheAgentCook/index.html` directly, with syntax highlighting and the QR code inlined.
The exercises live in [ArnavBallinCode/let-the-agent-cook](https://github.com/ArnavBallinCode/let-the-agent-cook).
