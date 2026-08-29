# gui_blocks.png

Rebuilds the inventory and hotbar icon sheet from a modern Minecraft client jar.

```sh
python3 tools/gui/render_gui_blocks.py . data/images/gui/gui_blocks.png \
        <jar>/assets/minecraft/textures data/images/gui/gui_blocks.png
```

The shipped sheet is both the input and the output, so any icon the tables below
do not cover keeps its original art instead of turning into a hole.

## Layout

One 512x512 sheet holds two unrelated grids.

- Isometric block previews, 48px cells, ten per row, slots 0..64.
- Flat item icons, 16px cells, thirty-two per row, starting at y=432.

`drawBlockIcon` rebases a block slot >= 128 onto the flat grid by subtracting 128,
so block slot 138 and item slot 10 are the same cell. `BLOCK_FLAT` in `items.py`
is therefore keyed by the rebased index, not the raw one.

## Files

- `isobox.py` renders a box in the same projection as the shipped art. The
  constants were fitted to it, so the silhouette matches to the pixel.
- `icons.py` maps the 65 isometric slots to block face textures.
- `items.py` maps flat slots to item textures.
- `render_gui_blocks.py` parses the slot numbers straight out of `src/world/item/item.h`
  and `src/gpu/item_icons.h`, so an engine rename shows up as a missing entry
  rather than silently painting the wrong cell.

## Notes

- A texture that vanilla tints at runtime ships greyscale, so wool stays white here
  and `hud.cpp` tints it. Leather is the exception, MCPSP does not tint it, so the
  default dye colour is baked in.
- Faces whose art is cut out do not make a solid cube. `stonecutter_side` is open
  under the table, which is why the sides come from `stonecutter_bottom`.
- Left unmapped on purpose, 49 glowing obsidian and 55 nether reactor are MCPE-only,
  and 57 trapdoor, 58 fence, 59 fence gate and 63 chest are shapes this renderer
  does not model. Flat slots 2 glass pane and 4 bed have no modern counterpart.
- Icon art from a Minecraft client jar is Mojang's and is not redistributable.
  Regenerate locally rather than committing a built sheet to a public tree.
