"""Inline logos, photos and QR codes into the talk deck.

Markers in the template:
  @@svg:name@@               simple-icon as inline <svg class="logo">
  @@svgat:name:x:y:size@@    simple-icon nested inside an SVG diagram
  @@img:file@@               data: URI for a file in ASSETS
  @@qr:url@@                 inline QR code SVG
"""
from __future__ import annotations

import base64
import io
import re
import sys
from pathlib import Path

import qrcode
import qrcode.image.svg

SCRATCH = Path(sys.argv[1])
OUT = Path(sys.argv[2])
LOGOS = SCRATCH / "logos"
ASSETS = SCRATCH / "assets"

template = (SCRATCH / "deck.template.html").read_text()


def icon_paths(name: str) -> str:
    svg = (LOGOS / f"{name}.svg").read_text()
    if name == "jitsi":
        inner = re.search(r"<svg[^>]*>(.*)</svg>", svg, re.S).group(1)
        inner = re.sub(r'fill="(?!none)[^"]*"', 'fill="currentColor"', inner)
        return inner
    return "".join(f'<path d="{d}"/>' for d in re.findall(r'<path d="([^"]+)"', svg))


def viewbox(name: str) -> str:
    svg = (LOGOS / f"{name}.svg").read_text()
    return re.search(r'viewBox="([^"]+)"', svg).group(1)


def svg_inline(m: re.Match) -> str:
    name = m.group(1)
    return f'<svg class="logo" viewBox="{viewbox(name)}" fill="currentColor" aria-hidden="true">{icon_paths(name)}</svg>'


def svg_at(m: re.Match) -> str:
    name, x, y, size = m.group(1), *m.group(2, 3, 4)
    return f'<svg x="{x}" y="{y}" width="{size}" height="{size}" viewBox="{viewbox(name)}" class="d-logo">{icon_paths(name)}</svg>'


def img(m: re.Match) -> str:
    f = ASSETS / m.group(1)
    mime = {"png": "image/png", "jpg": "image/jpeg", "svg": "image/svg+xml"}[f.suffix[1:]]
    return f"data:{mime};base64," + base64.b64encode(f.read_bytes()).decode()


def qr(m: re.Match) -> str:
    q = qrcode.QRCode(border=0, error_correction=qrcode.constants.ERROR_CORRECT_M)
    q.add_data(m.group(1))
    q.make(fit=True)
    buf = io.BytesIO()
    q.make_image(image_factory=qrcode.image.svg.SvgPathImage).save(buf)
    svg = buf.getvalue().decode()
    svg = re.sub(r"<\?xml[^>]*\?>", "", svg)
    d = re.search(r'<path[^>]* d="([^"]+)"', svg).group(1)
    vb = re.search(r'viewBox="([^"]+)"', svg).group(1)
    return f'<svg class="qr-svg" viewBox="{vb}" aria-hidden="true"><path d="{d}" fill="currentColor"/></svg>'


html = re.sub(r"@@svgat:(\w+):([\d.]+):([\d.]+):([\d.]+)@@", svg_at, template)
html = re.sub(r"@@svg:(\w+)@@", svg_inline, html)
html = re.sub(r"@@img:([\w.]+)@@", img, html)
html = re.sub(r"@@qr:([^@]+)@@", qr, html)
OUT.write_text(html)
print(f"wrote {OUT} ({len(html) // 1024} KB)")
