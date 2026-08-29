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

# Vanilla water art is greyscale and coloured per biome at runtime, and MCPSP has
# no water tint, so these cells get recoloured from the shipped ones instead.
WATER_CELLS = {(13, 12), (14, 12)}


def load(path):
    return np.asarray(Image.open(path).convert("RGBA"), dtype=np.float64)


def is_cutout(tile):
    """True when alpha is strictly on or off, which is how the GE alpha test wants it."""
    a = tile[..., 3]
    return not ((a > 0) & (a < 255)).any()


def downscale(tile, n):
    """Box filter weighting colour by alpha, so transparent texels cannot wash it out.

    Dividing by an averaged alpha afterwards, the usual unpremultiply, amplifies
    colour wherever the footprint was mostly transparent and turns cutout foliage
    white.
    """
    f = tile.shape[0] // n
    b = tile.reshape(n, f, n, f, 4)
    a = b[..., 3]
    wsum = a.sum(axis=(1, 3))
    rgb = (b[..., :3] * a[..., None]).sum(axis=(1, 3)) / np.maximum(wsum, 1e-9)[..., None]

    # Keep a plausible colour under fully transparent texels so bilinear taps at
    # the edges do not pull black in.
    if wsum.sum() > 0:
        mean = (tile[..., :3] * tile[..., 3:4]).sum(axis=(0, 1)) / tile[..., 3].sum()
        rgb = np.where((wsum > 0)[..., None], rgb, mean)

    return rgb, a.mean(axis=(1, 3))


def resize(tile, n):
    if tile.shape[0] == n:
        return tile.copy()
    rgb, alpha = downscale(tile, n)
    # A cutout has to stay a cutout. Averaged alpha makes distant leaves and reeds
    # semi-transparent, and the sky behind them reads as glare.
    if is_cutout(tile):
        alpha = binarise(tile[..., 3], alpha, n)
    return np.concatenate([rgb, alpha[..., None]], axis=2)


def binarise(src_alpha, mean_alpha, n):
    """Pick the binary alpha that best keeps the tile's opaque fraction.

    Thresholding the box average eats thin detail, since a two texel sugar cane
    stalk straddles every block boundary and averages to exactly half. Point
    sampling keeps such a stalk but aliases denser art, so both are tried and
    whichever lands closest to the source coverage wins.
    """
    target = (src_alpha == 255).mean()
    f = src_alpha.shape[0] // n

    best = np.where(mean_alpha >= 128.0, 255.0, 0.0)
    err = abs((best > 0).mean() - target)
    for dy in range(f):
        for dx in range(f):
            cand = np.where(src_alpha[dy::f, dx::f] == 255, 255.0, 0.0)
            e = abs((cand > 0).mean() - target)
            if e < err:
                best, err = cand, e
    return best


def match_moments(tile, ref):
    """Recolour greyscale art onto the shipped tile's per-channel mean and spread.

    The shipped cell is the only record of what the renderer and the fog were tuned
    against, so matching its moments keeps that colour and its contrast while taking
    the modern wave shapes. Alpha is matched too, since a flat colour under a varying
    alpha reads as speckle once the mip levels average the colour away.
    """
    lum = tile[..., :3].mean(axis=2)
    sd = lum.std()
    z = (lum - lum.mean()) / sd if sd > 1e-6 else np.zeros_like(lum)
    out = ref.mean(axis=(0, 1)) + z[..., None] * ref.std(axis=(0, 1))
    return np.clip(out, 0.0, 255.0)


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
            tile = match_moments(tile, atlas[y:y + TILE, x:x + TILE])
        atlas[y:y + TILE, x:x + TILE] = tile
        used += 1

    Image.fromarray(np.clip(atlas, 0, 255).astype(np.uint8), "RGBA").save(out_png)
    print("%s  %d cells replaced, %d kept" % (out_png, used, kept))
    return atlas


def build_anim(src_dir, name, out_png):
    """Pass an animation strip through as RGBA; the engine plays a frame per tick."""
    a = load(os.path.join(src_dir, name + ".png"))
    Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGBA").save(out_png)
    print("%s  %d frames" % (out_png, a.shape[0] // a.shape[1]))


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
    build_anim(src, "nether_portal", os.path.join(outdir, "portal.png"))
