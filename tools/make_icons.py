#!/usr/bin/env python3
# Genera el atlas de iconos del editor (src/ui/kit/icons_atlas.png) y la
# cabecera src/ui/kit/icons.h a partir de los SVG de Lucide.
#
# Lucide (https://lucide.dev) se distribuye bajo licencia ISC; los SVG se
# descargan del repositorio oficial y NO se versionan aqui. Este script
# rasteriza los trazados a blanco sobre transparente (el tinte se aplica
# en tiempo de ejecucion con WIC/GDI en IconAtlas) a 16/20/24/32 px, monta
# el atlas y escribe los identificadores.
#
# Uso:  python tools/make_icons.py
#
# No necesita cairosvg ni ninguna dependencia nativa: incluye un
# rasterizador minimo de SVG (M L H V C S Q T A Z + formas basicas) basado
# en "sellar" discos a lo largo del trazado y reducir con LANCZOS, que da
# el mismo resultado suave que un stroke con remates redondos (Lucide usa
# stroke-linecap/join = round y stroke-width = 2).

import io
import math
import os
import re
import sys
import urllib.request

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "tools", "icons_cache")
OUT_PNG = os.path.join(ROOT, "src", "ui", "kit", "icons_atlas.png")
OUT_H = os.path.join(ROOT, "src", "ui", "kit", "icons.h")

RAW = "https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/{}.svg"

# Escalas (px de celda) del atlas, en el orden en que se apilan las bandas.
SCALES = [16, 20, 24, 32]
# Factor de supermuestreo para el antialias (se reduce con LANCZOS).
OVERSAMPLE = 4
VIEW = 24.0  # viewBox de Lucide

# (nombre del enum, fichero Lucide, fichero de reserva si el primero da 404)
ICONS = [
    # --- 19 categorias -------------------------------------------------
    ("CatOrganization", "folder", "square"),
    ("CatLighting", "sun", "square"),
    ("CatLightSources", "lightbulb", "square"),
    ("CatPhysics", "cog", "square"),
    ("CatParticles", "sparkles", "square"),
    ("CatInteraction", "mouse-pointer", "square"),
    ("CatAnimations", "clapperboard", "square"),
    ("CatAudio", "volume-2", "square"),
    ("CatInput", "gamepad-2", "square"),
    ("CatCommunication", "radio", "square"),
    ("CatScripts", "file-code", "square"),
    ("CatTime", "alarm-clock", "square"),
    ("CatResources", "package", "square"),
    ("CatDiagnostics", "bug", "square"),
    ("CatPersistence", "database", "square"),
    ("CatRuntime", "wrench", "square"),
    ("CatGui", "image", "square"),
    ("CatInterpolation", "activity", "square"),
    ("CatValues", "hash", "square"),
    # --- 15 clases comunes --------------------------------------------
    ("Part", "box", "square"),
    ("Model", "boxes", "square"),
    ("Folder", "folder", "square"),
    ("Workspace", "layout-dashboard", "square"),
    ("Camera", "camera", "square"),
    ("Lighting", "sun-medium", "sun"),
    ("Script", "file-code", "square"),
    ("LocalScript", "file-terminal", "file"),
    ("ModuleScript", "file-cog", "file"),
    ("Light", "lightbulb", "square"),
    ("Sound", "volume-2", "square"),
    ("Dimension", "layers", "square"),
    ("Attachment", "paperclip", "square"),
    ("Decal", "image", "square"),
    ("ParticleEmitter", "sparkles", "square"),
    # --- interfaz ------------------------------------------------------
    ("UiBook", "book-open", "square"),
    ("UiPlus", "plus", "square"),
    ("UiPlay", "play", "square"),
    ("UiStop", "square", "square"),
]

NUM = r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?"
TOKEN = re.compile(r"([MmLlHhVvCcSsQqTtAaZz])|(" + NUM + r")")
ATTR = re.compile(r'([a-zA-Z-]+)\s*=\s*"([^"]*)"')


def fetch(name):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, name + ".svg")
    if not os.path.exists(path):
        url = RAW.format(name)
        with urllib.request.urlopen(url, timeout=30) as r:
            data = r.read()
        with open(path, "wb") as f:
            f.write(data)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def attrs(tag):
    return {k: v for k, v in ATTR.findall(tag)}


def nums(s):
    return [float(x) for x in re.findall(NUM, s)]


# --- conversion de arco a cubica (parametrizacion de punto final) --------
def arc_to_cubics(x1, y1, rx, ry, phi, large, sweep, x2, y2):
    if rx == 0 or ry == 0:
        return [(x1, y1, x2, y2, x2, y2)]
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p = cp * dx + sp * dy
    y1p = -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1:
        s = math.sqrt(lam)
        rx *= s
        ry *= s
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den))
    if large == sweep:
        co = -co
    cxp = co * rx * y1p / ry
    cyp = -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2.0
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2.0

    def ang(ux, uy, vx, vy):
        d = (ux * vx + uy * vy) / (math.hypot(ux, uy) * math.hypot(vx, vy))
        a = math.acos(max(-1.0, min(1.0, d)))
        return -a if (ux * vy - uy * vx) < 0 else a

    th1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dth = ang((x1p - cxp) / rx, (y1p - cyp) / ry,
              (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dth > 0:
        dth -= 2 * math.pi
    elif sweep and dth < 0:
        dth += 2 * math.pi
    segs = max(1, int(math.ceil(abs(dth) / (math.pi / 2.0))))
    out = []
    for i in range(segs):
        a0 = th1 + dth * i / segs
        a1 = th1 + dth * (i + 1) / segs
        k = (4.0 / 3.0) * math.tan((a1 - a0) / 4.0)

        def pt(a):
            x = cx + rx * math.cos(a) * cp - ry * math.sin(a) * sp
            y = cy + rx * math.cos(a) * sp + ry * math.sin(a) * cp
            return x, y

        def der(a):
            x = -rx * math.sin(a) * cp - ry * math.cos(a) * sp
            y = -rx * math.sin(a) * sp + ry * math.cos(a) * cp
            return x, y

        x0, y0 = pt(a0)
        x3, y3 = pt(a1)
        dx0, dy0 = der(a0)
        dx3, dy3 = der(a1)
        out.append((x0 + k * dx0, y0 + k * dy0, x3 - k * dx3, y3 - k * dy3, x3, y3))
    return out


def flatten_cubic(p0, p1, p2, p3):
    # Numero de tramos segun la longitud de la poligonal de control.
    approx = (math.dist(p0, p1) + math.dist(p1, p2) + math.dist(p2, p3))
    n = max(2, min(24, int(approx * 0.8)))
    pts = []
    for i in range(1, n + 1):
        t = i / n
        mt = 1 - t
        x = (mt ** 3 * p0[0] + 3 * mt * mt * t * p1[0]
             + 3 * mt * t * t * p2[0] + t ** 3 * p3[0])
        y = (mt ** 3 * p0[1] + 3 * mt * mt * t * p1[1]
             + 3 * mt * t * t * p2[1] + t ** 3 * p3[1])
        pts.append((x, y))
    return pts


def parse_path(d):
    """Devuelve una lista de subcaminos; cada uno es una lista de puntos.

    Escaner manual (no regex) porque la sintaxis compacta de los arcos
    ("...0 0022 17") mete los flags binarios pegados a los numeros y hay
    que leerlos caracter a caracter.
    """
    n = len(d)
    i = 0
    subs = []
    cur = []
    x = y = 0.0
    sx = sy = 0.0
    prev = None  # control reflejado para S/T
    cmd = None

    def skip():
        nonlocal i
        while i < n and d[i] in " \t\r\n,":
            i += 1

    def number():
        nonlocal i
        skip()
        j = i
        if i < n and d[i] in "+-":
            i += 1
        while i < n and d[i].isdigit():
            i += 1
        if i < n and d[i] == ".":
            i += 1
            while i < n and d[i].isdigit():
                i += 1
        if i < n and d[i] in "eE":
            i += 1
            if i < n and d[i] in "+-":
                i += 1
            while i < n and d[i].isdigit():
                i += 1
        return float(d[j:i])

    def flag():
        nonlocal i
        skip()
        c = d[i]
        i += 1
        return float(c)

    while True:
        skip()
        if i >= n:
            break
        if d[i].isalpha():
            cmd = d[i]
            i += 1
        elif cmd is None:
            break
        upper = cmd.upper()
        rel = cmd.islower()
        if upper == "M":
            a, b = number(), number()
            if rel:
                a, b = x + a, y + b
            if cur:
                subs.append(cur)
            cur = [(a, b)]
            x, y = a, b
            sx, sy = a, b
            prev = None
            cmd = "l" if rel else "L"
        elif upper == "L":
            a, b = number(), number()
            if rel:
                a, b = x + a, y + b
            cur.append((a, b))
            x, y = a, b
            prev = None
        elif upper == "H":
            a = number()
            if rel:
                a = x + a
            cur.append((a, y))
            x = a
            prev = None
        elif upper == "V":
            a = number()
            if rel:
                a = y + a
            cur.append((x, a))
            y = a
            prev = None
        elif upper == "C":
            a, b, c, e, f, g = (number() for _ in range(6))
            if rel:
                a, b, c, e, f, g = x + a, y + b, x + c, y + e, x + f, y + g
            cur += flatten_cubic((x, y), (a, b), (c, e), (f, g))
            prev = (c, e)
            x, y = f, g
        elif upper == "S":
            a, b, c, e = (number() for _ in range(4))
            if rel:
                a, b, c, e = x + a, y + b, x + c, y + e
            cx, cy = prev if prev else (x, y)
            cur += flatten_cubic((x, y), (2 * x - cx, 2 * y - cy), (a, b), (c, e))
            prev = (a, b)
            x, y = c, e
        elif upper == "Q":
            a, b, c, e = (number() for _ in range(4))
            if rel:
                a, b, c, e = x + a, y + b, x + c, y + e
            c1 = (x + 2.0 / 3.0 * (a - x), y + 2.0 / 3.0 * (b - y))
            c2 = (c + 2.0 / 3.0 * (a - c), e + 2.0 / 3.0 * (b - e))
            cur += flatten_cubic((x, y), c1, c2, (c, e))
            prev = (a, b)
            x, y = c, e
        elif upper == "T":
            a, b = number(), number()
            if rel:
                a, b = x + a, y + b
            cx, cy = prev if prev else (x, y)
            q = (2 * x - cx, 2 * y - cy)
            c1 = (x + 2.0 / 3.0 * (q[0] - x), y + 2.0 / 3.0 * (q[1] - y))
            c2 = (a + 2.0 / 3.0 * (q[0] - a), b + 2.0 / 3.0 * (q[1] - b))
            cur += flatten_cubic((x, y), c1, c2, (a, b))
            prev = q
            x, y = a, b
        elif upper == "A":
            rx, ry, rot = number(), number(), number()
            large, sweep = int(flag()), int(flag())
            a, b = number(), number()
            if rel:
                a, b = x + a, y + b
            for c in arc_to_cubics(x, y, rx, ry, rot, large, sweep, a, b):
                cur += flatten_cubic((x, y), (c[0], c[1]), (c[2], c[3]), (c[4], c[5]))
                x, y = c[4], c[5]
            prev = None
            x, y = a, b
        elif upper == "Z":
            cur.append((sx, sy))
            x, y = sx, sy
            prev = None
            if cur:
                subs.append(cur)
            cur = []
        else:
            break
    if cur:
        subs.append(cur)
    return subs


def shape_subpaths(text):
    """Extrae los puntos (viewBox 0..24) de todos los elementos trazados."""
    out = []
    for m in re.finditer(r"<(path|line|circle|rect|polyline|polygon|ellipse)\b([^>]*)>", text):
        kind, body = m.group(1), m.group(2)
        a = attrs(body)
        if kind == "path":
            out += parse_path(a.get("d", ""))
        elif kind == "line":
            out.append([(float(a.get("x1", 0)), float(a.get("y1", 0))),
                        (float(a.get("x2", 0)), float(a.get("y2", 0)))])
        elif kind == "circle":
            cx, cy, r = float(a.get("cx", 0)), float(a.get("cy", 0)), float(a.get("r", 0))
            out.append([(cx + r * math.cos(t), cy + r * math.sin(t))
                        for t in [i * math.tau / 48 for i in range(49)]])
        elif kind == "ellipse":
            cx, cy = float(a.get("cx", 0)), float(a.get("cy", 0))
            rx, ry = float(a.get("rx", 0)), float(a.get("ry", 0))
            out.append([(cx + rx * math.cos(t), cy + ry * math.sin(t))
                        for t in [i * math.tau / 48 for i in range(49)]])
        elif kind == "rect":
            x0, y0 = float(a.get("x", 0)), float(a.get("y", 0))
            w, h = float(a.get("width", 0)), float(a.get("height", 0))
            out.append([(x0, y0), (x0 + w, y0), (x0 + w, y0 + h), (x0, y0 + h), (x0, y0)])
        elif kind == "polyline":
            out.append([tuple(p) for p in zip(nums(a.get("points", ""))[0::2],
                                              nums(a.get("points", ""))[1::2])])
        elif kind == "polygon":
            pts = [tuple(p) for p in zip(nums(a.get("points", ""))[0::2],
                                         nums(a.get("points", ""))[1::2])]
            if pts:
                pts.append(pts[0])
            out.append(pts)
    return out


def rasterize(svg, size):
    subs = shape_subpaths(svg)
    hi = size * OVERSAMPLE
    k = hi / VIEW
    img = Image.new("L", (hi, hi), 0)
    dr = ImageDraw.Draw(img)
    r = max(1.0, (2.0 * k) / 2.0)  # radio del trazo (stroke-width 2)
    for sub in subs:
        pts = [(px * k, py * k) for (px, py) in sub]
        for i in range(len(pts) - 1):
            x0, y0 = pts[i]
            x1, y1 = pts[i + 1]
            seg = math.dist((x0, y0), (x1, y1))
            steps = max(1, int(seg / max(1.0, r * 0.5)))
            for s in range(steps + 1):
                t = s / steps
                x = x0 + (x1 - x0) * t
                y = y0 + (y1 - y0) * t
                dr.ellipse([x - r, y - r, x + r, y + r], fill=255)
    return img.resize((size, size), Image.LANCZOS)


def load_icon(name, fallback):
    for cand in (name, fallback, "square"):
        try:
            return fetch(cand)
        except Exception as e:  # noqa: BLE001
            print("  aviso: no se pudo descargar %s (%s)" % (cand, e))
    return "<svg></svg>"


def main():
    os.makedirs(os.path.dirname(OUT_PNG), exist_ok=True)
    count = len(ICONS)
    # Rasteriza cada icono a cada escala.
    rendered = {}
    for idx, (enum, name, fallback) in enumerate(ICONS):
        svg = load_icon(name, fallback)
        rendered[enum] = {s: rasterize(svg, s) for s in SCALES}
        print("[%2d/%d] %s <- %s.svg" % (idx + 1, count, enum, name))

    # Monta el atlas: una banda horizontal por escala, apiladas.
    width = max(s * count for s in SCALES)
    height = sum(SCALES)
    atlas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    y = 0
    for s in SCALES:
        for idx, (enum, _n, _f) in enumerate(ICONS):
            cov = rendered[enum][s]
            cell = Image.new("RGBA", (s, s), (255, 255, 255, 0))
            cell.putalpha(cov)
            atlas.paste(cell, (idx * s, y))
        y += s
    atlas.save(OUT_PNG)
    print("atlas ->", OUT_PNG, atlas.size)

    # Cabecera generada.
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
        f.write("// GENERADO por tools/make_icons.py. NO EDITAR A MANO.\n")
        f.write("// Iconos de Lucide (ISC); atlas embebido como RCDATA.\n")
        f.write("#pragma once\n\nnamespace sk {\nnamespace ui {\n\n")
        f.write("// Identificador de icono (indice de celda en el atlas).\n")
        f.write("enum class IconId : int {\n")
        for enum, _n, _f in ICONS:
            f.write("    %s,\n" % enum)
        f.write("    Count,\n};\n\n")
        f.write("inline constexpr int kIconCount = static_cast<int>(IconId::Count);\n\n")
        f.write("// Escalas (px de celda) presentes en el atlas, banda a banda.\n")
        f.write("inline constexpr int kIconScales[] = {%s};\n"
                % ", ".join(str(s) for s in SCALES))
        f.write("inline constexpr int kIconScaleCount = %d;\n\n" % len(SCALES))
        f.write("// Nombre del SVG de Lucide de cada icono (para regenerar).\n")
        f.write("inline constexpr const char* kIconNames[] = {\n")
        for _e, n, _f in ICONS:
            f.write('    "%s",\n' % n)
        f.write("};\n\n")
        f.write("// Id del recurso RCDATA del atlas (ver app.rc).\n")
        f.write("inline constexpr int kIconAtlasResId = 101;\n\n")
        f.write("} // namespace ui\n} // namespace sk\n")
    print("header ->", OUT_H)


if __name__ == "__main__":
    main()
