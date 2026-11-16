#!/usr/bin/env python3
"""Generates the bundled presets (spec v0.4, 19.2, 19.3) into presets/ja and
presets/en, and the preset based sample stacks (UI spec 3.5) into
samples/ja and samples/en. The CSV based sample ("連札") is made by
build/host/make_samples (tools/MakeSamples.cpp) with the core's CSV import.

The IDs are derived from the names, so the output is reproducible. They are
replaced with fresh IDs whenever a stack is created from a preset (19.2).

    python3 tools/make_presets.py
"""
import base64
import hashlib
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
CARD_W, CARD_H = 512, 342


def b32(seed, length):
    digest = hashlib.sha256(seed.encode("utf-8")).digest()
    return base64.b32encode(digest).decode("ascii").lower()[:length]


def doc_id(seed):
    return b32("id:" + seed, 26)


def part_id(seed, prefix="f"):
    return prefix + "_" + b32("part:" + seed, 6)


def dump(obj):
    # Same layout as the core writer: 2 spaces, LF, "bdb" first.
    text = json.dumps(obj, ensure_ascii=False, indent=2)
    text = re.sub(r"\[\s+(-?\d+),\s+(-?\d+),\s+(-?\d+),\s+(-?\d+)\s+\]",
                  r"[\1, \2, \3, \4]", text)
    return text + "\n"


def sanitize(title):
    for c in '/:*?"<>|\\':
        title = title.replace(c, "_")
    return title.strip(" .")[:40] or "無題"


LABEL_X, LABEL_W = 12, 88
FIELD_X = 104


def field(stack, key, name, kind, y, w=396, h=24, x=FIELD_X,
          multiline=False, decimals=0):
    """A value part with its label. `key` keeps the IDs equal between the
    languages, so that a card made from one preset fits the other."""
    pid = part_id(stack + ":" + key)
    part = {"id": pid, "name": name, "kind": kind, "rect": [x, y, w, h]}
    if kind == "text":
        part["default"] = ""
        part["multiline"] = multiline
    elif kind == "number":
        part["default"] = None
        part["decimals"] = decimals
    elif kind == "image":
        part["default"] = None
        part["fit"] = "contain"
    label = {"id": part_id(stack + ":label:" + key, "l"), "kind": "label",
             "rect": [LABEL_X if x == FIELD_X else x,
                      y + 3 if x == FIELD_X else y - 22, LABEL_W, 20],
             "text": name}
    return [label, part]


def navigation(stack):
    """Previous, position, next at the bottom (19.2, UI 6.10)."""
    y = CARD_H - 12 - 28
    total = 60 + 8 + 72 + 8 + 60
    x = (CARD_W - total) // 2
    return [
        {"id": part_id(stack + ":nav:prev", "b"), "kind": "button",
         "rect": [x, y, 60, 28], "text": "◀", "action": "card.prev"},
        {"id": part_id(stack + ":nav:counter", "c"), "kind": "counter",
         "rect": [x + 68, y + 4, 72, 20]},
        {"id": part_id(stack + ":nav:next", "b"), "kind": "button",
         "rect": [x + 148, y, 60, 28], "text": "▶", "action": "card.next"},
    ]


# Layouts shared by both languages: (key, kind, y, w, h, x, multiline)
ADDRESS = [
    ("name", "text", 10, 396, 24, FIELD_X, False),
    ("kana", "text", 38, 396, 24, FIELD_X, False),
    ("zip", "text", 66, 120, 24, FIELD_X, False),
    ("address", "text", 94, 396, 48, FIELD_X, True),
    ("phone", "text", 148, 200, 24, FIELD_X, False),
    ("mail", "text", 176, 396, 24, FIELD_X, False),
    ("birthday", "text", 204, 120, 24, FIELD_X, False),
    ("memo", "text", 232, 396, 56, FIELD_X, True),
]
BOOKS = [
    ("title", "text", 10, 256, 24, FIELD_X, False),
    ("author", "text", 38, 256, 24, FIELD_X, False),
    ("publisher", "text", 66, 256, 24, FIELD_X, False),
    ("isbn", "text", 94, 160, 24, FIELD_X, False),
    ("price", "number", 122, 100, 24, FIELD_X, False),
    ("pages", "number", 150, 100, 24, FIELD_X, False),
    ("bought", "text", 178, 120, 24, FIELD_X, False),
    ("memo", "text", 206, 396, 82, FIELD_X, True),
    ("cover", "image", 34, 128, 154, 372, False),
]
CARDS = [
    ("name", "text", 10, 256, 24, FIELD_X, False),
    ("company", "text", 38, 256, 24, FIELD_X, False),
    ("section", "text", 66, 256, 24, FIELD_X, False),
    ("image", "image", 34, 128, 66, 372, False),
    ("phone", "text", 94, 200, 24, FIELD_X, False),
    ("mail", "text", 122, 260, 24, FIELD_X, False),
    ("address", "text", 150, 396, 48, FIELD_X, True),
    ("received", "text", 204, 120, 24, FIELD_X, False),
    ("memo", "text", 232, 396, 56, FIELD_X, True),
]

NAMES = {
    "ja": {
        "address": ("住所録", "氏名、住所、電話、メールなどを管理します。", {
            "name": "氏名", "kana": "ふりがな", "zip": "郵便番号",
            "address": "住所", "phone": "電話", "mail": "メール",
            "birthday": "誕生日", "memo": "メモ"}),
        "books": ("書籍管理", "本のタイトル、著者、価格、読書メモ、表紙の画像を"
                  "管理します。", {
            "title": "タイトル", "author": "著者", "publisher": "出版社",
            "isbn": "ISBN", "price": "価格", "pages": "ページ数",
            "bought": "購入日", "memo": "読書メモ", "cover": "表紙"}),
        "cards": ("名刺管理", "受け取った名刺の情報と、名刺の画像を管理します。", {
            "name": "氏名", "company": "会社名", "section": "部署・役職",
            "image": "名刺の画像", "phone": "電話", "mail": "メール",
            "address": "住所", "received": "受け取った日", "memo": "メモ"}),
    },
    "en": {
        "address": ("Address Book", "Names, addresses, phone numbers and "
                    "e-mail addresses.", {
            "name": "Name", "kana": "Reading", "zip": "Postal code",
            "address": "Address", "phone": "Phone", "mail": "E-mail",
            "birthday": "Birthday", "memo": "Notes"}),
        "books": ("Books", "Titles, authors, prices, reading notes and cover "
                  "images of your books.", {
            "title": "Title", "author": "Author", "publisher": "Publisher",
            "isbn": "ISBN", "price": "Price", "pages": "Pages",
            "bought": "Purchased", "memo": "Notes", "cover": "Cover"}),
        "cards": ("Business Cards", "Business cards you received, with a "
                  "picture of each card.", {
            "name": "Name", "company": "Company", "section": "Dept. / Title",
            "image": "Card image", "phone": "Phone", "mail": "E-mail",
            "address": "Address", "received": "Received", "memo": "Notes"}),
    },
}

LAYOUTS = {"address": (ADDRESS, "name"), "books": (BOOKS, "title"),
           "cards": (CARDS, "name")}

# Sample cards (UI spec 3.5): fictitious people, public domain books.
SAMPLE_CARDS = {
    "ja": {
        "address": [
            {"name": "山田 太郎", "kana": "やまだ たろう", "zip": "100-0001",
             "address": "東京都千代田区千代田1-1\n(サンプルの住所)",
             "phone": "03-0000-0000", "mail": "taro@example.com",
             "birthday": "1980-04-01", "memo": "サンプルのカードです。"},
            {"name": "佐藤 花子", "kana": "さとう はなこ", "zip": "530-0001",
             "address": "大阪府大阪市北区梅田1-1\n(サンプルの住所)",
             "phone": "06-0000-0000", "mail": "hanako@example.com",
             "birthday": "1985-10-10", "memo": ""},
            {"name": "鈴木 一郎", "kana": "すずき いちろう", "zip": "460-0001",
             "address": "愛知県名古屋市中区1-1\n(サンプルの住所)",
             "phone": "052-000-0000", "mail": "ichiro@example.com",
             "birthday": "1990-12-24", "memo": "年賀状を送る"},
        ],
        "books": [
            {"title": "吾輩は猫である", "author": "夏目漱石",
             "publisher": "(サンプル)", "isbn": "", "price": 800,
             "pages": 480, "bought": "2026-01-15",
             "memo": "猫の目から見た人間の世界。"},
            {"title": "羅生門", "author": "芥川龍之介",
             "publisher": "(サンプル)", "isbn": "", "price": 400,
             "pages": 120, "bought": "2026-02-01", "memo": ""},
            {"title": "銀河鉄道の夜", "author": "宮沢賢治",
             "publisher": "(サンプル)", "isbn": "", "price": 500,
             "pages": 200, "bought": "2026-03-10",
             "memo": "ジョバンニとカムパネルラの旅。"},
        ],
    },
    "en": {
        "address": [
            {"name": "Jane Doe", "kana": "", "zip": "00000",
             "address": "1 Example Street\nSampletown",
             "phone": "+1 555 0100", "mail": "jane@example.com",
             "birthday": "1980-04-01", "memo": "A sample card."},
            {"name": "John Smith", "kana": "", "zip": "00001",
             "address": "2 Example Avenue\nSampleville",
             "phone": "+1 555 0101", "mail": "john@example.com",
             "birthday": "1985-10-10", "memo": ""},
        ],
        "books": [
            {"title": "Pride and Prejudice", "author": "Jane Austen",
             "publisher": "(sample)", "isbn": "", "price": 9.5,
             "pages": 432, "bought": "2026-01-15",
             "memo": "It is a truth universally acknowledged..."},
            {"title": "Moby-Dick", "author": "Herman Melville",
             "publisher": "(sample)", "isbn": "", "price": 12,
             "pages": 720, "bought": "2026-02-01", "memo": ""},
            {"title": "Alice's Adventures in Wonderland",
             "author": "Lewis Carroll", "publisher": "(sample)", "isbn": "",
             "price": 7.25, "pages": 200, "bought": "2026-03-10",
             "memo": "Down the rabbit hole."},
        ],
    },
}
SAMPLE_TITLES = {
    "ja": {"address": "住所録のサンプル", "books": "書籍管理のサンプル"},
    "en": {"address": "Address Book Sample", "books": "Books Sample"},
}


def make_template(stack_key, lang):
    title, description, names = NAMES[lang][stack_key]
    layout, title_key = LAYOUTS[stack_key]
    parts = []
    for key, kind, y, w, h, x, multiline in layout:
        parts += field(stack_key, key, names[key], kind, y, w=w, h=h, x=x,
                       multiline=multiline)
    parts += navigation(stack_key)
    check_layout(title, parts)
    template_id = doc_id(lang + ":" + stack_key + ":template")
    template = {
        "bdb": "template",
        "format_version": 1,
        "template_id": template_id,
        "title": title,
        "rev": 1,
        "size": {"w": CARD_W, "h": CARD_H},
        "theme": "beos",
        "title_part": part_id(stack_key + ":" + title_key),
        "parts": parts,
    }
    return title, description, template


def check_layout(title, parts):
    # Parts must not overlap and must stay on the card (19.3).
    rects = [p["rect"] for p in parts]
    for i, a in enumerate(rects):
        for b in rects[i + 1:]:
            if (a[0] < b[0] + b[2] and b[0] < a[0] + a[2]
                    and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]):
                sys.exit("overlap in %s: %s %s" % (title, a, b))
        if a[0] + a[2] > CARD_W or a[1] + a[3] > CARD_H:
            sys.exit("outside the card in %s: %s" % (title, a))


def write_stack(stack_dir, title, template, manifest_extra, cards):
    os.makedirs(os.path.join(stack_dir, "templates"))
    os.makedirs(os.path.join(stack_dir, "cards"))
    template_id = template["template_id"]
    manifest = {
        "bdb": "manifest",
        "format_version": 1,
        "stack_id": doc_id(os.path.basename(stack_dir) + ":stack"),
        "title": title,
        "default_template": template_id,
        "encryption": None,
    }
    manifest.update(manifest_extra)
    name = "%s-%s.rfdt" % (sanitize(template["title"]), template_id[:6])
    with open(os.path.join(stack_dir, "templates", name), "w",
              encoding="utf-8", newline="\n") as f:
        f.write(dump(template))
    with open(os.path.join(stack_dir, sanitize(title) + ".rfdm"), "w",
              encoding="utf-8", newline="\n") as f:
        f.write(dump(manifest))
    for index, card in enumerate(cards):
        card_title = card["values"].get(template["title_part"], "")
        card_title = card_title.split("\n")[0].strip() or "無題"
        name = "%s-%s.rfdd" % (sanitize(card_title), card["card_id"][:6])
        with open(os.path.join(stack_dir, "cards", name), "w",
                  encoding="utf-8", newline="\n") as f:
            f.write(dump(card))


def build_presets(lang):
    out_dir = os.path.join(ROOT, "presets", lang)
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)
    for key in ("address", "books", "cards"):
        title, description, template = make_template(key, lang)
        write_stack(os.path.join(out_dir, sanitize(title) + ".rfdf"), title,
                    template, {"preset_description": description}, [])
    print("wrote", out_dir)


def build_samples(lang):
    out_dir = os.path.join(ROOT, "samples", lang)
    os.makedirs(out_dir, exist_ok=True)
    for key, rows in SAMPLE_CARDS[lang].items():
        title = SAMPLE_TITLES[lang][key]
        stack_dir = os.path.join(out_dir, sanitize(title) + ".rfdf")
        if os.path.isdir(stack_dir):
            shutil.rmtree(stack_dir)
        _, _, template = make_template(key, lang)
        cards = []
        for index, row in enumerate(rows):
            values = {}
            for field_key, value in row.items():
                values[part_id(key + ":" + field_key)] = value
            cards.append({
                "bdb": "card",
                "format_version": 1,
                "card_id": doc_id("%s:%s:card:%d" % (lang, key, index)),
                "template_id": template["template_id"],
                "seq": (index + 1) * 1024,
                "rev": 1,
                "created": "2026-10-15T00:00:00Z",
                "modified": "2026-10-15T00:00:00Z",
                "values": values,
            })
        write_stack(stack_dir, title, template, {}, cards)
    print("wrote", out_dir)


if __name__ == "__main__":
    for language in ("ja", "en"):
        build_presets(language)
        build_samples(language)
