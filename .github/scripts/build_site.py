"""Assemble the GitHub Pages site.

Every top-level folder (and category subfolders like collegetalks/*) that holds an
index.html is a talk. It is copied to _site/ and listed on the generated landing
page at _site/index.html.
Folders and files whose names start with "_" or "." are not published.
"""
from __future__ import annotations

import html
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / "_site"


def skip(name: str) -> bool:
    return name.startswith(("_", "."))


def meta(page: str, pattern: str) -> str:
    m = re.search(pattern, page, re.I | re.S)
    return html.unescape(m.group(1).strip()) if m else ""


def pretty(folder: str) -> str:
    if folder.lower() == "collegetalks":
        return "College Talks"
    if folder == "DistributedKeyValueStore":
        return "Distributed Key-Value Store"
    # OpenTechSummit2026 -> Open Tech Summit 2026
    return re.sub(r"(?<=[a-z])(?=[A-Z0-9])|(?<=[0-9])(?=[A-Z])", " ", folder).replace("-", " ").replace("_", " ")


def last_updated(folder: Path) -> str:
    out = subprocess.run(
        ["git", "log", "-1", "--format=%cs", "--", str(folder)],
        cwd=ROOT, capture_output=True, text=True,
    )
    return out.stdout.strip()


def collect() -> list[dict[str, str]]:
    talks = []
    for folder in sorted(ROOT.iterdir()):
        if not folder.is_dir() or skip(folder.name):
            continue

        # Check for category subfolders with nested talks (e.g. collegetalks/ConsistentHashing)
        nested_talks = [
            sub for sub in sorted(folder.iterdir())
            if sub.is_dir() and not skip(sub.name) and (sub / "index.html").exists()
        ]

        if nested_talks:
            for sub in nested_talks:
                page = (sub / "index.html").read_text(encoding="utf-8", errors="ignore")[:20000]
                talks.append({
                    "slug": f"{folder.name}/{sub.name}",
                    "event": f"{pretty(folder.name)} · {pretty(sub.name)}",
                    "title": meta(page, r"<title>(.*?)</title>") or f"{pretty(folder.name)}: {pretty(sub.name)}",
                    "description": meta(page, r'<meta\s+name="description"\s+content="(.*?)"'),
                    "date": last_updated(sub) or last_updated(folder),
                })

        # If the folder has its own index.html and no nested talks, index it directly
        elif (folder / "index.html").exists():
            page = (folder / "index.html").read_text(encoding="utf-8", errors="ignore")[:20000]
            talks.append({
                "slug": folder.name,
                "event": pretty(folder.name),
                "title": meta(page, r"<title>(.*?)</title>") or pretty(folder.name),
                "description": meta(page, r'<meta\s+name="description"\s+content="(.*?)"'),
                "date": last_updated(folder),
            })

    talks.sort(key=lambda t: t["date"], reverse=True)
    return talks


def copy_talks() -> None:
    for folder in sorted(ROOT.iterdir()):
        if folder.is_dir() and not skip(folder.name):
            shutil.copytree(
                folder, SITE / folder.name,
                ignore=lambda _dir, names: [n for n in names if skip(n)],
            )


def landing(talks: list[dict[str, str]]) -> str:
    items = "\n".join(
        f"""      <li><a href="./{html.escape(t['slug'])}/">
        <span class="event">{html.escape(t['event'])}</span>
        <span class="title">{html.escape(t['title'])}</span>
        {f'<span class="desc">{html.escape(t["description"])}</span>' if t['description'] else ''}
        <span class="date">{html.escape(t['date'])}</span>
      </a></li>"""
        for t in talks
    ) or '      <li class="empty">No talks yet. Add a folder with an index.html.</li>'
    return f"""<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Talks · Arnav Angarkar</title>
<meta name="description" content="Slides from talks and workshops by Arnav Angarkar.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,800&family=IBM+Plex+Sans:wght@400;500&family=IBM+Plex+Mono&display=swap">
<style>
  :root {{ --night: #0b0e1b; --panel: #141a2d; --line: #2a3150; --ink: #eeecf6; --mute: #959bb8; --tally: #ffb340; color-scheme: dark; }}
  * {{ box-sizing: border-box; }}
  body {{ margin: 0; min-height: 100vh; background: radial-gradient(60vmax 50vmax at 0% 0%, rgba(255,179,64,.12), transparent 60%), var(--night); color: var(--ink); font: 17px/1.5 "IBM Plex Sans", system-ui, sans-serif; padding: 12vh max(20px, env(safe-area-inset-right)) 10vh max(20px, env(safe-area-inset-left)); }}
  main {{ max-width: 860px; margin: 0 auto; display: flex; flex-direction: column; gap: 2.5rem; }}
  .eyebrow {{ font: 12px "IBM Plex Mono", monospace; letter-spacing: .16em; text-transform: uppercase; color: var(--tally); }}
  h1 {{ font: 800 clamp(2.6rem, 8vw, 5rem)/1 "Bricolage Grotesque", system-ui, sans-serif; letter-spacing: -.03em; margin: .4rem 0 0; }}
  ul {{ list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 1rem; }}
  a {{ display: grid; grid-template-columns: 1fr auto; gap: .2rem 1rem; padding: 1.3rem 1.4rem; background: var(--panel); border: 1px solid var(--line); border-radius: 14px; color: inherit; text-decoration: none; transition: border-color .2s, transform .2s; }}
  a:hover, a:focus-visible {{ border-color: var(--tally); transform: translateY(-2px); outline: none; }}
  .event {{ font: 12px "IBM Plex Mono", monospace; letter-spacing: .12em; text-transform: uppercase; color: var(--tally); }}
  .date {{ font: 12px "IBM Plex Mono", monospace; color: var(--mute); grid-column: 2; grid-row: 1; }}
  .title {{ grid-column: 1 / -1; font: 800 1.6rem/1.15 "Bricolage Grotesque", system-ui, sans-serif; letter-spacing: -.02em; }}
  .desc {{ grid-column: 1 / -1; color: var(--mute); }}
  .empty {{ color: var(--mute); }}
  footer {{ font: 12px "IBM Plex Mono", monospace; color: var(--mute); }}
  footer a {{ display: inline; padding: 0; background: none; border: 0; color: var(--tally); }}
</style>
<main>
  <header>
    <div class="eyebrow">Arnav Angarkar</div>
    <h1>Talks</h1>
  </header>
  <ul>
{items}
  </ul>
  <footer><a href="https://github.com/arnavballincode">github.com/arnavballincode</a></footer>
</main>
</html>
"""


def main() -> None:
    if SITE.exists():
        shutil.rmtree(SITE)
    SITE.mkdir()
    copy_talks()
    talks = collect()
    (SITE / "index.html").write_text(landing(talks), encoding="utf-8")
    (SITE / ".nojekyll").write_text("")
    print(f"Built {len(talks)} talk(s): {', '.join(t['slug'] for t in talks)}")


if __name__ == "__main__":
    main()
