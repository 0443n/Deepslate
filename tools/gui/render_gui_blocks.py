#!/usr/bin/env python3
"""Rebuild gui_blocks.png from modern Minecraft textures.

Two independent regions share the sheet. Isometric block previews sit in 48px cells
ten to a row, and flat item icons sit in 16px cells thirty-two to a row starting at
y=432. Slot numbers are parsed out of the engine headers rather than restated, so
this cannot drift from what hud.cpp actually indexes.
"""
import os, re, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from isobox import render, CELL
from icons import ICONS
from items import ITEMS, SPECIAL, COAL, DYE, BLOCK_FLAT

FLAT = 16
FLAT_ROW0 = 27

# Vanilla tints leather at runtime and ships the art greyscale. MCPSP tints only
# wool, so the default dye colour has to be baked in here.
LEATHER = (0xA0, 0x65, 0x40)


def parse_slots(root):
    """slot lookups straight from src/, so a rename in the engine surfaces as a miss."""
    item_h = open(os.path.join(root, "src/world/item/item.h")).read()
    body = item_h[item_h.index("enum {"):item_h.index("enum { DYE_WHITE")]
    ids = {m.group(1): int(m.group(2))
           for m in re.finditer(r"(ITEM_[A-Z0-9_]+)\s*=\s*(\d+)", body)}

    ic = open(os.path.join(root, "src/gpu/item_icons.h")).read()

    def array(name):
        seg = ic[ic.index(name):]
        return [int(x) for x in re.findall(r"-?\d+", seg[seg.index("{") + 1:seg.index("}")])]

    icon = array("kItemIcon[256]")
    slots = {}
    for name, iid in ids.items():
        idx = iid - 256
        if 0 <= idx < len(icon) and icon[idx] >= 0:
            slots[name] = icon[idx]
    for m in re.finditer(r"#define\s+(II_[A-Z0-9_]+)\s+(\d+)", ic):
        slots[m.group(1)] = int(m.group(2))
    return slots, array("kItemIconCoal[16]"), array("kItemIconDye[16]")


def load(tex_root, name, size=None):
    sub, base = ("block", name[6:]) if name.startswith("block:") else ("item", name)
    a = np.asarray(Image.open(os.path.join(tex_root, sub, base + ".png")).convert("RGBA"),
                   dtype=np.float64)
    h, w = a.shape[:2]
    if h > w:
        a = a[:w]
    if size and a.shape[0] != size:
        a = np.asarray(Image.fromarray(a.astype(np.uint8), "RGBA").resize((size, size), Image.BOX),
                       dtype=np.float64)
    return a


def tinted(tex, rgb):
    if rgb is None:
        return tex
    out = tex.copy()
    out[..., :3] *= np.array(rgb, dtype=np.float64) / 255.0
    return out


def over(dst, src):
    a = src[..., 3:4] / 255.0
    return np.concatenate([dst[..., :3] * (1 - a) + src[..., :3] * a,
                           np.maximum(dst[..., 3:4], src[..., 3:4])], axis=2)


def flat_art(tex_root, name):
    """Leather is greyscale plus an untinted overlay, everything else is used as is."""
    tex = load(tex_root, name, FLAT)
    if name.startswith("leather_"):
        tex = tinted(tex, LEATHER)
        ov = os.path.join(tex_root, "item", name + "_overlay.png")
        if os.path.exists(ov):
            tex = over(tex, load(tex_root, name + "_overlay", FLAT))
    return tex


def build(root, sheet_png, tex_root, out_png):
    sheet = np.asarray(Image.open(sheet_png).convert("RGBA"), dtype=np.float64).copy()
    cubes = 0
    for idx, spec in sorted(ICONS.items()):
        face = lambda k: load(tex_root, "block:" + spec[k], FLAT)
        top = tinted(face("top"), spec["top_tint"] or spec["tint"])
        left = tinted(face("left"), spec["tint"])
        right = tinted(face("right"), spec["tint"])
        shape = spec["shape"]
        icon = render([(b, top, left, right)
                       for b in (shape if isinstance(shape, list) else [shape])])
        x, y = (idx % 10) * CELL, (idx // 10) * CELL
        sheet[y:y + CELL, x:x + CELL] = icon
        cubes += 1

    slots, coal, dye = parse_slots(root)
    want = {}
    for key, tex in list(ITEMS.items()) + list(SPECIAL.items()):
        if key in slots:
            want[slots[key]] = tex
    for data, tex in COAL.items():
        if coal[data] >= 0:
            want[coal[data]] = tex
    for data, tex in DYE.items():
        if dye[data] >= 0:
            want[dye[data]] = tex
    want.update(BLOCK_FLAT)

    for slot, tex in sorted(want.items()):
        x, y = (slot & 31) * FLAT, (FLAT_ROW0 + (slot >> 5)) * FLAT
        sheet[y:y + FLAT, x:x + FLAT] = flat_art(tex_root, tex)

    Image.fromarray(np.clip(sheet, 0, 255).astype(np.uint8), "RGBA").save(out_png)
    print("%s  %d block icons, %d flat icons" % (out_png, cubes, len(want)))
    unmapped = [k for k in ITEMS if k not in slots]
    if unmapped:
        print("  items with no slot in kItemIcon:", ", ".join(sorted(unmapped)))


if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
