# terrain.png generator

MCPSP addresses terrain as a 16x16 grid of tiles in the legacy Beta `terrain.png`
layout. Modern Minecraft ships one PNG per block instead, so this rebuilds the
atlas from a modern texture set.

`map.py` holds the correspondence, derived from `Tile::getTexture` and its helpers
in `src/world/level/tile/`. It covers every cell the engine actually addresses -
120 mapped to modern art, 15 left as they were (chest, bed, and the MCPE-only
blocks that have no modern counterpart).

## Regenerating

Extract a client jar's `assets/minecraft/textures/block` somewhere, then:

    python3 tools/terrain/compose.py \
        data/images/terrain.png <block-dir> data/images

The first argument is the base atlas, used for the cells that stay legacy, so
running against the shipped `terrain.png` is correct. Writes `terrain.png` plus
the two mip levels.

## Notes

- Cells the engine tints (grass, leaves, foliage, melon stem) must stay greyscale.
  Modern vanilla already ships them that way.
- Water is the exception - it is greyscale and biome-coloured at runtime, and
  MCPSP applies no water tint, so the overworld colour is baked in.
- Mip levels are downscaled per tile. Resizing the whole atlas at once would
  bleed neighbouring tiles into each other.
- Block art from a Minecraft client jar is Mojang's and is not redistributable.
  Regenerate locally rather than committing a built atlas to a public tree.
