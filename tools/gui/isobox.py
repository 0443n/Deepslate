"""Isometric box rasteriser matching the projection baked into gui_blocks.png.

Geometry was measured off the shipped atlas rather than guessed. A cube occupies
x 3..44 and y 1..46 of its 48px cell, the top face diamond is 20px tall, and the
vertical edge is 25px. Boxes cover cubes, slabs and stairs alike, so one renderer
serves every shape the icon sheet needs.
"""
import numpy as np

CELL = 48
CX, TOP_Y = 24.0, 0.5
HW, HD, VH = 21.0, 10.5, 26.0

# Measured off the existing icons. The left face reads brighter than the right.
SHADE_TOP, SHADE_LEFT, SHADE_RIGHT = 1.0, 0.71, 0.52

# Faces meeting at the same screen pixel are resolved by this view direction.
VIEW = np.array([1.0, 0.8, 1.0])


def project(p):
    x, y, z = p
    return np.array([CX + (x - z) * HW, TOP_Y + (x + z) * HD + (1.0 - y) * VH])


def _faces(box):
    """Top, left and right quads of a box as (origin, du, dv, shade, depth-corner)."""
    x0, y0, z0, x1, y1, z1 = box
    return [
        ((x0, y1, z0), (x1, y1, z0), (x0, y1, z1), SHADE_TOP),   # top,   u=x v=z
        ((x0, y1, z1), (x1, y1, z1), (x0, y0, z1), SHADE_LEFT),  # left,  u=x v=y
        ((x1, y1, z1), (x1, y1, z0), (x1, y0, z1), SHADE_RIGHT),  # right, u=z v=y
    ]


def render(boxes, size=CELL):
    """boxes: list of (box, top_tex, left_tex, right_tex). Textures are 16x16 RGBA."""
    out = np.zeros((size, size, 4), dtype=np.float64)
    depth = np.full((size, size), -1e9)
    ys, xs = np.mgrid[0:size, 0:size]
    px = np.stack([xs + 0.5, ys + 0.5], axis=-1)

    for box, *texs in boxes:
        for (o, a, b, shade), tex in zip(_faces(box), texs):
            if tex is None:
                continue
            o3, a3, b3 = np.array(o), np.array(a), np.array(b)
            p0, du, dv = project(o3), project(a3) - project(o3), project(b3) - project(o3)
            m = np.array([[du[0], dv[0]], [du[1], dv[1]]])
            det = m[0, 0] * m[1, 1] - m[0, 1] * m[1, 0]
            if abs(det) < 1e-9:
                continue
            inv = np.array([[m[1, 1], -m[0, 1]], [-m[1, 0], m[0, 0]]]) / det
            d = px - p0
            u = d[..., 0] * inv[0, 0] + d[..., 1] * inv[0, 1]
            v = d[..., 0] * inv[1, 0] + d[..., 1] * inv[1, 1]

            inside = (u >= 0) & (u < 1) & (v >= 0) & (v < 1)
            if not inside.any():
                continue

            n = tex.shape[0]
            tu = np.clip((u * n).astype(int), 0, n - 1)
            tv = np.clip((v * n).astype(int), 0, n - 1)
            texel = tex[tv, tu]
            inside &= texel[..., 3] > 0

            world = o3 + u[..., None] * (a3 - o3) + v[..., None] * (b3 - o3)
            dep = world @ VIEW
            take = inside & (dep > depth)

            rgb = texel[..., :3] * shade
            out[take] = np.concatenate([rgb, texel[..., 3:4]], axis=-1)[take]
            depth[take] = dep[take]

    return out


CUBE  = (0.0, 0.0, 0.0, 1.0, 1.0, 1.0)
SLAB  = (0.0, 0.0, 0.0, 1.0, 0.5, 1.0)
# Lower slab plus the raised back half, which is how the shipped stair icons sit.
STAIRS = [SLAB, (0.0, 0.5, 0.0, 1.0, 1.0, 0.5)]
