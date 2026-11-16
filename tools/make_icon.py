#!/usr/bin/env python3
"""Renfuda (連札) application icon: three tall slips (札) fanned out, in the
BeOS icon style (light from the top left, bold dark outlines, a soft
shadow below to the right).

Writes the HVIF data (Haiku vector icon) into app/BDB.rdef as the
vector_icon resource, and a PNG preview:

    tools/make_icon.py [preview.png]

and app/Renfuda.hvif (the icon file; `make hpkg` adds it as the
executable's BEOS:ICON attribute).

The HVIF encoder covers what the icon needs: solid and linear gradient
styles, straight paths, path source shapes and stroke transformers
(see src/libs/icon/flat_icon in the Haiku tree).
"""
import math
import os
import re
import struct
import sys

# ---- design ---------------------------------------------------------------
# Icon space: 64 x 64 units.

OUTLINE = (40, 26, 16, 255)


def rotated_rect(cx, cy, w, h, angle):
    """Corners (top left, top right, bottom right, bottom left)."""
    a = math.radians(angle)
    ca, sa = math.cos(a), math.sin(a)
    pts = []
    for dx, dy in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2),
                   (-w / 2, h / 2)):
        pts.append((cx + dx * ca - dy * sa, cy + dx * sa + dy * ca))
    return pts


def card_local(cx, cy, w, h, angle):
    """Maps card local coordinates (0..1 across, 0..1 down) to icon space."""
    a = math.radians(angle)
    ca, sa = math.cos(a), math.sin(a)

    def f(u, v):
        dx, dy = (u - 0.5) * w, (v - 0.5) * h
        return (cx + dx * ca - dy * sa, cy + dx * sa + dy * ca)
    return f


def local_rect(f, u0, v0, u1, v1):
    return [f(u0, v0), f(u1, v0), f(u1, v1), f(u0, v1)]


def local_circle(f, u, v, ru, rv, n=14):
    return [f(u + ru * math.cos(2 * math.pi * i / n),
              v + rv * math.sin(2 * math.pi * i / n)) for i in range(n)]


def gradient(p0, p1, stops):
    return ("gradient", p0, p1, stops)


def solid(color):
    return ("solid", color)


class Icon:
    """Shapes in drawing order: (points, style, stroke width or 0)."""

    def __init__(self):
        self.shapes = []

    def add(self, points, style, stroke=0, stroke_style=None):
        self.shapes.append((points, style, 0))
        if stroke:
            self.shapes.append((points, stroke_style or solid(OUTLINE),
                                stroke))


W, H = 21, 44
INK = (30, 24, 22, 255)

BLUE = dict(light=(120, 170, 240), dark=(30, 70, 160), band=(200, 225, 255))
RED = dict(light=(255, 150, 110), dark=(200, 40, 30), band=(255, 230, 120))
CREAM = dict(light=(255, 252, 236), dark=(226, 206, 160), band=(214, 40, 30))


def brush(f, w, h, u0, v0, u1, v1, w0, w1):
    """A brush stroke from (u0, v0) to (u1, v1), w0 wide at the start and
    w1 at the end (icon units), on a card of w x h."""
    du, dv = (u1 - u0) * w, (v1 - v0) * h
    n = math.hypot(du, dv) or 1
    nu, nv = -dv / n / w, du / n / h
    return [f(u0 + nu * w0, v0 + nv * w0), f(u1 + nu * w1, v1 + nv * w1),
            f(u1 - nu * w1, v1 - nv * w1), f(u0 - nu * w0, v0 - nv * w0)]


def draw_slip(icon, cx, cy, w, h, angle, colors, content="plain",
              shadow=True, outline=2):
    """One slip (札): paper, bevel, the band at the top and its content:
    "plain" (a pale line), "satsu" (札 and a seal), "template" (field
    frames) or "theme" (color chips)."""
    f = card_local(cx, cy, w, h, angle)
    if shadow:
        icon.add(rotated_rect(cx + 3, cy + 3, w, h, angle),
                 solid((0, 0, 0, 70)))
    light, dark = colors["light"], colors["dark"]
    icon.add(rotated_rect(cx, cy, w, h, angle),
             gradient(f(0, 0), f(1, 1),
                      [(0, light + (255,)), (255, dark + (255,))]),
             stroke=outline)
    # A light edge on the top and the left (the BeOS bevel).
    icon.add([f(0.07, 0.95), f(0.07, 0.035), f(0.93, 0.035), f(0.93, 0.06),
              f(0.13, 0.06), f(0.13, 0.95)], solid((255, 255, 255, 150)))
    # The band at the top, like a senjafuda's name plate.
    band = colors["band"]
    icon.add(local_rect(f, 0.16, 0.08, 0.84, 0.26),
             gradient(f(0.16, 0.08), f(0.84, 0.26),
                      [(0, tuple(min(255, c + 30) for c in band) + (255,)),
                       (255, band + (255,))]),
             stroke=1, stroke_style=solid((60, 30, 20, 200)))
    if content == "plain":
        icon.add(local_rect(f, 0.42, 0.34, 0.58, 0.86),
                 solid((255, 255, 255, 90)))
    elif content == "satsu":
        # 札: 木 (a bar across, a stroke down, two falling strokes) and the
        # hook on the right; a red seal below.
        def b(*a):
            icon.add(brush(f, w, h, *a), solid(INK))
        b(0.30, 0.38, 0.62, 0.37, 1.3, 1.0)
        b(0.46, 0.31, 0.47, 0.70, 1.3, 0.7)
        b(0.45, 0.42, 0.30, 0.56, 1.0, 0.5)
        b(0.49, 0.43, 0.60, 0.52, 1.0, 0.6)
        b(0.68, 0.32, 0.67, 0.66, 1.2, 1.0)
        b(0.67, 0.66, 0.76, 0.60, 1.0, 0.5)
        icon.add(local_circle(f, 0.62, 0.83, 0.13, 0.065),
                 gradient(f(0.5, 0.77), f(0.75, 0.9),
                          [(0, (240, 80, 60, 255)),
                           (255, (180, 20, 20, 255))]),
                 stroke=1, stroke_style=solid((120, 20, 20, 255)))
    elif content == "template":
        # Empty field frames, dashed like the edit mode's guides.
        for v0, v1 in ((0.33, 0.43), (0.49, 0.59), (0.65, 0.88)):
            icon.add(local_rect(f, 0.18, v0, 0.82, v1),
                     solid((255, 255, 255, 230)), stroke=1,
                     stroke_style=solid((50, 90, 170, 255)))
    elif content == "theme":
        chips = [(230, 60, 40), (250, 200, 40), (60, 170, 80),
                 (50, 110, 220)]
        for k, color in enumerate(chips):
            v0 = 0.34 + k * 0.14
            icon.add(local_rect(f, 0.25, v0, 0.75, v0 + 0.10),
                     solid(color + (255,)), stroke=1)


def app_icon():
    icon = Icon()
    slips = [((21, 31), -16, BLUE), ((31, 29), -3, RED),
             ((42, 32), 12, CREAM)]
    for (cx, cy), angle, _ in slips:
        icon.add(rotated_rect(cx + 3, cy + 3, W, H, angle),
                 solid((0, 0, 0, 70)))
    for k, ((cx, cy), angle, colors) in enumerate(slips):
        draw_slip(icon, cx, cy, W, H, angle, colors,
                  "satsu" if k == 2 else "plain", shadow=False)
    return icon


def stack_icon():
    """A stack (the .rfdf folder and its manifest): a BeOS folder with the
    three slips standing in it."""
    icon = Icon()
    folder_dark = (196, 150, 50)
    folder_light = (255, 222, 120)
    # Shadow of the folder.
    icon.add([(9, 30), (59, 30), (61, 60), (11, 60)], solid((0, 0, 0, 70)))
    # Back of the folder with its tab.
    back = [(5, 22), (20, 22), (23, 18), (36, 18), (38, 22), (55, 22),
            (55, 54), (5, 54)]
    icon.add(back, gradient((5, 18), (55, 54),
                            [(0, (236, 190, 80, 255)),
                             (255, (176, 130, 40, 255))]), stroke=2)
    # The slips, standing up out of the folder.
    sw, sh = 15, 34
    for (cx, cy), angle, colors, content in (
            ((19, 25), -12, BLUE, "plain"), ((29, 22), -2, RED, "plain"),
            ((40, 25), 10, CREAM, "satsu")):
        draw_slip(icon, cx, cy, sw, sh, angle, colors, content,
                  shadow=False, outline=2)
    # Front of the folder, tilted towards the viewer.
    front = [(3, 34), (57, 34), (54, 58), (6, 58)]
    icon.add(front, gradient((3, 34), (54, 58),
                             [(0, folder_light + (255,)),
                              (255, folder_dark + (255,))]), stroke=2)
    # Its light top edge.
    icon.add([(5, 35.5), (55, 35.5), (55, 37), (5, 37)],
             solid((255, 255, 255, 150)))
    return icon


def single_icon(content, colors):
    icon = Icon()
    draw_slip(icon, 30, 31, 25, 50, 0, colors, content)
    return icon


ICONS = {
    # resource name -> builder
    "app": app_icon,
    "stack": stack_icon,
    "card": lambda: single_icon("satsu", CREAM),
    "template": lambda: single_icon("template", BLUE),
    "theme": lambda: single_icon("theme", CREAM),
}


# ---- HVIF ------------------------------------------------------------------

def coord(v):
    v = max(-128.0, min(192.0, v))
    if abs(v - round(v)) < 1e-6 and -32 <= v <= 95:
        return bytes([int(round(v)) + 32])
    value = int((v + 128.0) * 102.0) | 0x8000
    return bytes([value >> 8, value & 0xff])


def float24(value):
    if value == 0:
        return b"\0\0\0"
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    sign = bits >> 31
    exponent = ((bits >> 23) & 0xff) - 127
    mantissa = bits & 0x7fffff
    if exponent >= 32 or exponent < -32:
        return b"\0\0\0"
    v = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
    return bytes([v >> 16, (v >> 8) & 0xff, v & 0xff])


def encode_style(style):
    if style[0] == "solid":
        r, g, b, a = style[1]
        if a == 255:
            return bytes([3, r, g, b])
        return bytes([1, r, g, b, a])
    _, (x0, y0), (x1, y1), stops = style
    # The gradient runs from -64 to 64 along x; the matrix maps it onto
    # p0 -> p1 (agg order: sx, shy, shx, sy, tx, ty).
    dx, dy = x1 - x0, y1 - y0
    sx, shy = dx / 128.0, dy / 128.0
    shx, sy = -shy, sx
    tx, ty = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    data = bytes([2, 0, 0x02, len(stops)])
    for v in (sx, shy, shx, sy, tx, ty):
        data += float24(v)
    for offset, (r, g, b, a) in stops:
        data += bytes([offset, r, g, b, a])
    return data


def build_hvif(shapes):
    styles, style_index = [], {}
    paths = []
    shape_data = []
    for points, style, stroke in shapes:
        key = repr(style)
        if key not in style_index:
            style_index[key] = len(styles)
            styles.append(style)
        paths.append(points)
        flags = 0x10 if stroke else 0
        data = bytes([10, style_index[key], 1, len(paths) - 1, flags])
        if stroke:
            # Stroke transformer: width, join (round) | cap (round) << 4,
            # miter limit.
            data += bytes([1, 23, 128 + stroke, 1 | (1 << 4), 4])
        shape_data.append(data)
    out = b"ncif" + bytes([len(styles)])
    for style in styles:
        out += encode_style(style)
    out += bytes([len(paths)])
    for points in paths:
        out += bytes([0x02 | 0x08, len(points)])
        for x, y in points:
            out += coord(x) + coord(y)
    out += bytes([len(shape_data)]) + b"".join(shape_data)
    return out


# ---- preview ---------------------------------------------------------------

def preview(shapes, path, size=256):
    from PIL import Image, ImageDraw
    scale = size / 64.0
    ss = 4
    big = size * ss
    image = Image.new("RGBA", (big, big), (216, 216, 216, 255))
    for points, style, stroke in shapes:
        poly = [(x * scale * ss, y * scale * ss) for x, y in points]
        mask = Image.new("L", (big, big), 0)
        draw = ImageDraw.Draw(mask)
        if stroke:
            draw.line(poly + [poly[0]], fill=255,
                      width=max(1, int(stroke * scale * ss)), joint="curve")
        else:
            draw.polygon(poly, fill=255)
        if style[0] == "solid":
            layer = Image.new("RGBA", (big, big), style[1])
        else:
            _, (x0, y0), (x1, y1), stops = style
            layer = Image.new("RGBA", (big, big))
            box = mask.getbbox()
            if box is None:
                continue
            px = layer.load()
            dx, dy = x1 - x0, y1 - y0
            length = dx * dx + dy * dy
            for yy in range(box[1], box[3]):
                for xx in range(box[0], box[2]):
                    ux = xx / (scale * ss) - x0
                    uy = yy / (scale * ss) - y0
                    t = max(0.0, min(1.0, (ux * dx + uy * dy) / length))
                    t *= 255
                    for (o0, c0), (o1, c1) in zip(stops, stops[1:]):
                        if o0 <= t <= o1:
                            k = (t - o0) / max(1, o1 - o0)
                            px[xx, yy] = tuple(int(c0[j] + (c1[j] - c0[j])
                                                   * k) for j in range(4))
                            break
        alpha = Image.new("L", (big, big), 0)
        alpha.paste(layer.getchannel("A"), mask=mask)
        layer.putalpha(alpha)
        image = Image.alpha_composite(image, layer)
    image = image.resize((size, size), Image.LANCZOS)
    small = image.resize((32, 32), Image.LANCZOS).resize((128, 128),
                                                        Image.NEAREST)
    sheet = Image.new("RGBA", (size + 140, size), (216, 216, 216, 255))
    sheet.paste(image, (0, 0))
    sheet.paste(small, (size + 6, 0))
    return sheet


MIME = {
    "stack": "application/x-vnd.renfuda-manifest",
    "card": "application/x-vnd.renfuda-data",
    "template": "application/x-vnd.renfuda-template",
    "theme": "application/x-vnd.renfuda-theme",
}


def hex_lines(data):
    hexes = data.hex().upper()
    return "\n".join('\t$"%s"' % hexes[i:i + 64]
                     for i in range(0, len(hexes), 64))


def write_rdef(icons):
    """The application icon, the icons of the document types (Tracker
    shows them for the files: "BEOS:L:<type>") and the stack icon by name
    for the stack folders (resource "renfuda:stack")."""
    here = os.path.dirname(os.path.abspath(__file__))
    rdef = os.path.join(here, "..", "app", "BDB.rdef")
    text = open(rdef, encoding="utf-8").read()
    block = ("// Icons (tools/make_icon.py).\n"
             "resource vector_icon {\n" + hex_lines(icons["app"]) + "\n};\n")
    number = 200
    for key, mime in MIME.items():
        block += ('\nresource(%d, "BEOS:L:%s") #\'VICN\' array {\n%s\n};\n'
                  % (number, mime, hex_lines(icons[key])))
        number += 1
    block += ('\nresource(%d, "renfuda:stack") #\'VICN\' array {\n%s\n};\n'
              % (number, hex_lines(icons["stack"])))
    block += "// End of icons.\n"
    pattern = re.compile(r"// (The application icon|Icons) \(tools/make_icon"
                         r"\.py\)\.\n.*?(// End of icons\.\n|\n\};\n(?!\nresource))",
                         re.S)
    if pattern.search(text):
        text = pattern.sub(lambda m: block, text, count=1)
    else:
        text = text.rstrip() + "\n\n" + block
    open(rdef, "w", encoding="utf-8").write(text)


if __name__ == "__main__":
    icons = {}
    sheets = []
    for name, builder in ICONS.items():
        shapes = builder().shapes
        icons[name] = build_hvif(shapes)
        print("%-9s %4d bytes, %2d shapes" % (name, len(icons[name]),
                                             len(shapes)))
        if len(sys.argv) > 1:
            sheets.append(preview(shapes, None))
    write_rdef(icons)
    # The application icon as a file, for the package's BEOS:ICON
    # attribute.
    here = os.path.dirname(os.path.abspath(__file__))
    open(os.path.join(here, "..", "app", "Renfuda.hvif"), "wb").write(
        icons["app"])
    if sheets:
        from PIL import Image
        out = Image.new("RGBA", (sheets[0].width, sheets[0].height
                                 * len(sheets)), (216, 216, 216, 255))
        for k, sheet in enumerate(sheets):
            out.paste(sheet, (0, k * sheet.height))
        out.save(sys.argv[1])
