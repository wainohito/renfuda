#!/usr/bin/env python3
"""Generates the bundled themes (theme spec v0.1, chapter 6) into themes/ and
the built-in default theme core/ThemeDefault.inc (a copy of beos.rfds that
is compiled into the core, so that a stack always has a theme).

    python3 tools/make_themes.py
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")


def theme(tid, name, desc, native, colors, font, kinds, focus):
    return {"bdb": "theme", "format_version": 1, "theme_id": tid, "rev": 1,
            "name": name, "description": desc, "native": native,
            "colors": colors, "font": font, "kinds": kinds, "focus": focus}


def bevel(look, width=2, fill=None, pressed=None, **extra):
    kind = {"look": look, "border": {"visible": True, "width": width}}
    if fill is not None:
        kind["fill"] = fill
    if pressed:
        kind["pressed"] = pressed
    kind.update(extra)
    return kind


BLANK = {"look": "flat", "border": {"visible": True, "width": 1}}

THEMES = [
    theme("beos", {"ja": "BeOS", "en": "BeOS"},
          {"ja": "BeOS R5 風の、立体的な見た目",
           "en": "A beveled look in the style of BeOS R5"}, False,
          {"panel": "#d8d8d8", "text": "#000000", "text_disabled": "#808080",
           "control_fill": "#ffffff", "control_border": "#808080",
           "accent": "#ffcb00", "link": "#0000ee", "bevel_light": "#ffffff",
           "bevel_dark": "#909090", "bevel_darker": "#606060"},
          {"family": "", "style": "Regular", "size": 12},
          {"text": bevel("sunken", fill="#ffffff"),
           "number": bevel("sunken", fill="#ffffff"),
           "bool": bevel("raised"), "image": bevel("sunken"),
           "label": {"look": "none"},
           "button": bevel("raised", fill="#d8d8d8", pressed="sunken"),
           "counter": bevel("sunken", width=1, fill="#e8e8e8"),
           "blank": BLANK},
          {"color": "#ffcb00", "width": 2}),
    theme("haiku", {"ja": "Haiku", "en": "Haiku"},
          {"ja": "システムの色と描画に従う",
           "en": "Follows the system colors and controls"}, True,
          {"panel": "#d8d8d8", "text": "#000000", "text_disabled": "#808080",
           "control_fill": "#ffffff", "control_border": "#acacac",
           "accent": "#0000e5", "link": "#0000ee", "bevel_light": "#ffffff",
           "bevel_dark": "#9e9e9e", "bevel_darker": "#747474"},
          {"family": "", "style": "Regular", "size": 12},
          {"text": bevel("sunken", width=1, fill="#ffffff"),
           "number": bevel("sunken", width=1, fill="#ffffff"),
           "bool": bevel("raised", width=1), "image": bevel("sunken", width=1),
           "label": {"look": "none"},
           "button": bevel("raised", width=1, fill="#e0e0e0",
                           pressed="sunken"),
           "counter": bevel("sunken", width=1, fill="#e8e8e8"),
           "blank": BLANK},
          {"color": "#0000e5", "width": 1}),
    theme("nextstep", {"ja": "NeXTSTEP", "en": "NeXTSTEP"},
          {"ja": "濃いグレーの面と、黒い外枠の二重の立体",
           "en": "Dark gray panels with double bevels and black outlines"},
          False,
          {"panel": "#aaaaaa", "text": "#000000", "text_disabled": "#555555",
           "control_fill": "#ffffff", "control_border": "#000000",
           "accent": "#333333", "link": "#000088", "bevel_light": "#ffffff",
           "bevel_dark": "#555555", "bevel_darker": "#000000"},
          {"family": "Helvetica", "style": "Regular", "size": 12},
          {"text": bevel("sunken2", fill="#ffffff"),
           "number": bevel("sunken2", fill="#ffffff"),
           "bool": bevel("raised2"), "image": bevel("sunken2"),
           "label": {"look": "none"},
           "button": bevel("raised2", fill="#aaaaaa", pressed="sunken2"),
           "counter": bevel("sunken", width=1, fill="#cccccc"),
           "blank": BLANK},
          {"color": "#000000", "width": 1}),
    theme("macos", {"ja": "Mac OS", "en": "Mac OS"},
          {"ja": "Mac OS 8〜9 の Platinum 風。角丸のボタン",
           "en": "Platinum style of Mac OS 8-9 with rounded buttons"}, False,
          {"panel": "#dddddd", "text": "#000000", "text_disabled": "#888888",
           "control_fill": "#ffffff", "control_border": "#000000",
           "accent": "#99aadd", "link": "#0000cc", "bevel_light": "#ffffff",
           "bevel_dark": "#888888", "bevel_darker": "#000000"},
          {"family": "Charcoal", "style": "Regular", "size": 12},
          {"text": bevel("sunken", width=1, fill="#ffffff"),
           "number": bevel("sunken", width=1, fill="#ffffff"),
           "bool": {"look": "round", "border": {"visible": True, "width": 1},
                    "radius": 4},
           "image": bevel("sunken", width=1), "label": {"look": "none"},
           "button": {"look": "round", "pressed": "round",
                      "border": {"visible": True, "width": 1},
                      "fill": "#eeeeee", "radius": 6},
           "counter": {"look": "flat", "border": {"visible": True, "width": 1},
                       "fill": "#ffffff"},
           "blank": BLANK},
          {"color": "#99aadd", "width": 2}),
    theme("win95", {"ja": "Windows 95", "en": "Windows 95"},
          {"ja": "灰色の面と二重の立体、紺色のアクセント",
           "en": "Gray panels, double bevels and a navy accent"}, False,
          {"panel": "#c0c0c0", "text": "#000000", "text_disabled": "#808080",
           "control_fill": "#ffffff", "control_border": "#000000",
           "accent": "#000080", "link": "#0000ff", "bevel_light": "#ffffff",
           "bevel_dark": "#808080", "bevel_darker": "#000000"},
          {"family": "MS Sans Serif", "style": "Regular", "size": 12},
          {"text": bevel("sunken2", fill="#ffffff"),
           "number": bevel("sunken2", fill="#ffffff"),
           "bool": bevel("raised2"), "image": bevel("sunken2"),
           "label": {"look": "none"},
           "button": bevel("raised2", fill="#c0c0c0", pressed="sunken2"),
           "counter": bevel("sunken", width=1, fill="#ffffff"),
           "blank": BLANK},
          {"color": "#000080", "width": 1}),
]


def main():
    out_dir = os.path.join(ROOT, "themes")
    os.makedirs(out_dir, exist_ok=True)
    for item in THEMES:
        text = json.dumps(item, ensure_ascii=False, indent=2) + "\n"
        with open(os.path.join(out_dir, item["theme_id"] + ".rfds"), "w",
                  encoding="utf-8", newline="\n") as f:
            f.write(text)
        if item["theme_id"] == "beos":
            with open(os.path.join(ROOT, "core", "ThemeDefault.inc"), "w",
                      encoding="utf-8", newline="\n") as f:
                f.write("// Generated by tools/make_themes.py from "
                        "themes/beos.rfds. Do not edit.\n")
                f.write('static const char* const kDefaultThemeData = R"BDB(')
                f.write(text)
                f.write(')BDB";\n')
    print("wrote", out_dir)


if __name__ == "__main__":
    main()
