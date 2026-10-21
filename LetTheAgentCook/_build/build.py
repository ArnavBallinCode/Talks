"""Build the Let the Agent Cook deck into one self-contained page.

    uv run --no-project --with qrcode==7.4.2 python LetTheAgentCook/_build/build.py

Reads the sources next to this file and writes LetTheAgentCook/index.html:

  shell.html    page skeleton with @@css@@, @@slides@@ and @@js@@ slots
  deck.css      styles
  slides.html   every slide and its speaker notes (edit this one)
  deck.js       navigation, builds, notes, presenter view

Markers inside slides.html:

  <pre class="code" data-lang="js" data-title="src/split.js" data-focus="1-3|5-9">
  raw code, no HTML escaping needed
  </pre>
      Syntax-highlighted at build time, one <span class="ln"> per line. data-focus lists the line
      ranges that light up on successive presses of the next key. Languages: js, sol, json, bash,
      md, text, diff.

  @@qr:https://example.com@@
      An inline QR code SVG.
"""
from __future__ import annotations

import html
import io
import re
import sys
import textwrap
from pathlib import Path

import qrcode
import qrcode.image.svg

HERE = Path(__file__).resolve().parent
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "index.html"

STR = r"\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'"
RULES: dict[str, list[tuple[str, str]]] = {
    "js": [
        ("c", r"//.*"),
        ("s", r"`(?:\\.|[^`\\])*`|" + STR),
        ("k", r"\b(?:import|from|export|const|let|var|function|return|if|else|for|of|in|new|throw|async|await|break|true|false|null|typeof)\b"),
        ("n", r"\b\d[\d_]*n?\b"),
        ("f", r"\b[A-Za-z_$][\w$]*(?=\s*\()"),
    ],
    "sol": [
        ("c", r"//.*"),
        ("s", STR),
        ("k", r"\b(?:pragma|solidity|contract|interface|function|external|public|private|internal|view|returns|return|if|revert|emit|error|event|mapping|memory|constant|import|require|new|for|bool|address|payable|uint256|uint16)\b"),
        ("n", r"\b\d[\d_]*\b"),
        ("f", r"\b[A-Za-z_]\w*(?=\s*[({])"),
    ],
    "json": [
        ("p", r"\"(?:\\.|[^\"\\])*\"(?=\s*:)"),
        ("s", r"\"(?:\\.|[^\"\\])*\""),
        ("k", r"\b(?:true|false|null)\b"),
        ("n", r"-?\b\d+(?:\.\d+)?\b"),
    ],
    "bash": [
        ("c", r"(?<!\S)#.*"),
        ("s", STR),
        ("v", r"\$\{?[A-Za-z_][A-Za-z0-9_]*\}?"),
        ("o", r"(?<=\s)--?[A-Za-z][\w-]*"),
        ("f", r"^(?:\$ )?[a-z/][\w./-]*"),
    ],
    "yaml": [
        ("c", r"(?<!\S)#.*"),
        ("p", r"^\s*[A-Za-z0-9_-]+(?=:)"),
        ("s", STR),
    ],
    "md": [
        ("h", r"^#{1,6} .*"),
        ("k", r"^\s*(?:[-*]|\d+\.)(?=\s)"),
        ("s", r"`[^`]+`"),
        ("b", r"\*\*[^*]+\*\*"),
        ("p", r"(?<![\w`])@[\w./-]+"),
    ],
}


def tokens(line: str, rules: list[tuple[str, str]]) -> str:
    pattern = "|".join(f"(?P<g{i}>{rx})" for i, (_, rx) in enumerate(rules))
    out, pos = [], 0
    for m in re.finditer(pattern, line):
        if m.start() == m.end():
            continue
        out.append(html.escape(line[pos:m.start()]))
        cls = rules[int(m.lastgroup[1:])][0]
        out.append(f'<span class="t-{cls}">{html.escape(m.group())}</span>')
        pos = m.end()
    out.append(html.escape(line[pos:]))
    return "".join(out)


def highlight(code: str, lang: str) -> list[str]:
    lines = code.split("\n")
    if lang == "md":
        result, in_front = [], False
        for n, line in enumerate(lines):
            if line == "---" and (n == 0 or in_front):
                in_front = n == 0
                result.append(f'<span class="t-m">{line}</span>')
            else:
                result.append(tokens(line, RULES["yaml" if in_front else "md"]))
        return result
    if lang == "text":
        out = []
        for line in lines:
            s = line.lstrip()
            cls = ("ok" if s.startswith(("✔", "[PASS]", "All "))
                   else "bad" if s.startswith(("✘", "[FAIL", "error"))
                   else "m" if s.startswith(("ℹ", "#", "warning", "Ran ", "Suite"))
                   else "")
            if line.startswith("$ "):
                out.append('<span class="t-o">$ </span>' + tokens(line[2:], RULES["bash"]))
            else:
                out.append(f'<span class="t-{cls}">{html.escape(line)}</span>' if cls else html.escape(line))
        return out
    if lang == "diff":
        return [f'<span class="t-{ {"+": "ok", "-": "bad", "@": "m"}.get(l[:1], "") }">{html.escape(l)}</span>' for l in lines]
    out = [tokens(line, RULES[lang]) for line in lines]
    if lang == "json":  # ${VAR} references inside strings stand out: they're where secrets come from
        out = [re.sub(r"\$\{[A-Za-z_]+\}", lambda m: f'<span class="t-v">{m.group()}</span>', l) for l in out]
    return out


def code_block(m: re.Match) -> str:
    attrs, body = m.group(1), m.group(2)
    lang = re.search(r'data-lang="([^"]+)"', attrs).group(1)
    title = re.search(r'data-title="([^"]*)"', attrs)
    code = textwrap.dedent(body.strip("\n")).rstrip()
    lines = "".join(f'<span class="ln">{l or " "}</span>' for l in highlight(code, lang))
    attrs = re.sub(r'\s*data-title="[^"]*"', "", attrs)
    caption = (f'<figcaption><span>{html.escape(title.group(1))}</span><span class="lang">{lang}</span></figcaption>'
               if title else "")
    return f'<figure class="codeblock">{caption}<pre class="code"{attrs}><code>{lines}</code></pre></figure>'


def qr(m: re.Match) -> str:
    q = qrcode.QRCode(border=0, error_correction=qrcode.constants.ERROR_CORRECT_M)
    q.add_data(m.group(1))
    q.make(fit=True)
    buf = io.BytesIO()
    q.make_image(image_factory=qrcode.image.svg.SvgPathImage).save(buf)
    svg = buf.getvalue().decode()
    d = re.search(r'<path[^>]* d="([^"]+)"', svg).group(1)
    vb = re.search(r'viewBox="([^"]+)"', svg).group(1)
    return f'<svg class="qr-svg" viewBox="{vb}" role="img" aria-label="QR code for {html.escape(m.group(1))}"><path d="{d}" fill="currentColor"/></svg>'


def main() -> None:
    read = lambda name: (HERE / name).read_text(encoding="utf-8")
    slides = re.sub(r'<pre class="code"([^>]*)>(.*?)</pre>', code_block, read("slides.html"), flags=re.S)
    slides = re.sub(r"@@qr:([^@]+)@@", qr, slides)
    page = (read("shell.html")
            .replace("@@css@@", read("deck.css"))
            .replace("@@slides@@", slides)
            .replace("@@js@@", read("deck.js")))
    OUT.write_text(page, encoding="utf-8")
    count = page.count('<section class="slide')
    print(f"wrote {OUT} ({len(page) // 1024} KB, {count} slides)")


if __name__ == "__main__":
    main()
