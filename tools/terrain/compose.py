#!/usr/bin/env python3
"""Rebuild data/images/terrain.png from a modern Minecraft resource pack.

MCPSP addresses a 16x16 grid of tiles in the legacy Beta terrain.png layout, while
modern packs ship one file per block. MAP in map.py carries that correspondence.
Cells mapped to None keep whatever the base atlas already had.
"""
import os, sys
import numpy as np
from PIL import Image
from map import MAP

TILE = 16
GRID = 16

# Vanilla water art is greyscale and coloured per biome at runtime. MCPSP has no
# water tint, so the overworld default gets baked in here.
WATER_CELLS = {(13, 12), (14, 12)}
WATER_RGB = (0x3F, 0x76, 0xE4)


def bake(rgb, colour):
    """Colour greyscale art so its mean lands on `colour` rather than well under it.

    Scaling by the peak leaves the mean short whenever the art has a few bright
    texels over a dark field, which is exactly the case for water.
    """
    lum = rgb.mean(axis=2, keepdims=True) / 255.0
    k = 1.0 / max(lum.mean(), 1e-6)
    for _ in range(8):
        out = np.clip(lum * k, 0.0, 1.0)
        err = out.mean()
        if abs(err - 1.0) < 1e-3 or err <= 0:
            break
        k *= min(4.0, 1.0 / err)
    return np.clip(lum * k, 0.0, 1.0) * np.array(colour, dtype=np.float64)


def load(path):
    return np.asarray(Image.open(path).convert("RGBA"), dtype=np.float64)


def resize(a, n):
    """Area-resize RGBA, premultiplying so transparent texels cannot darken edges."""
    h, w = a.shape[:2]
    if (h, w) == (n, n):
        return a.copy()
    rgb, alpha = a[..., :3], a[..., 3:4]
    pm = np.concatenate([rgb * (alpha / 255.0), alpha], axis=2)
    im = Image.fromarray(np.clip(pm, 0, 255).astype(np.uint8), "RGBA")
    pm = np.asarray(im.resize((n, n), Image.BOX), dtype=np.float64)
    al = pm[..., 3:4]
    out = np.concatenate([np.divide(pm[..., :3], al / 255.0,
                                    out=np.zeros_like(pm[..., :3]), where=al > 0), al], axis=2)
    return out


def first_frame(a):
    h, w = a.shape[:2]
    return a[:w] if h > w else a


def build(base_png, src_dir, out_png):
    atlas = load(base_png)
    if atlas.shape[0] != GRID * TILE:
        sys.exit("base atlas is %dpx, expected %d" % (atlas.shape[0], GRID * TILE))

    used = kept = 0
    for (col, row), name in sorted(MAP.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        y, x = row * TILE, col * TILE
        if name is None:
            kept += 1
            continue
        tile = resize(first_frame(load(os.path.join(src_dir, name + ".png"))), TILE)
        if (col, row) in WATER_CELLS:
            # Keep the original alpha so the water layer's blending is unchanged.
            tile = np.concatenate(
                [bake(tile[..., :3], WATER_RGB), atlas[y:y + TILE, x:x + TILE, 3:4]], axis=2)
        atlas[y:y + TILE, x:x + TILE] = tile
        used += 1

    Image.fromarray(np.clip(atlas, 0, 255).astype(np.uint8), "RGBA").save(out_png)
    print("%s  %d cells replaced, %d kept" % (out_png, used, kept))
    return atlas


def build_mip(atlas, n, out_png):
    """Downscale each tile on its own; a whole-atlas resize would bleed neighbours in."""
    out = np.zeros((GRID * n, GRID * n, 4), dtype=np.float64)
    for row in range(GRID):
        for col in range(GRID):
            t = atlas[row * TILE:(row + 1) * TILE, col * TILE:(col + 1) * TILE]
            out[row * n:(row + 1) * n, col * n:(col + 1) * n] = resize(t, n)
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGBA").save(out_png)
    print("%s  %dx%d" % (out_png, GRID * n, GRID * n))


if __name__ == "__main__":
    base, src, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
    a = build(base, src, os.path.join(outdir, "terrain.png"))
    build_mip(a, 8, os.path.join(outdir, "terrainMipMapLevel2.png"))
    build_mip(a, 4, os.path.join(outdir, "terrainMipMapLevel3.png"))
