"""gui_blocks.png icon slots, mapped to modern block textures.

Indices come from guiBlockIcon() and its helpers in src/client/gui/hud.cpp.
Slots left out keep the shipped art, either because the block is MCPE-only with no
modern counterpart or because its icon is a shape this renderer does not model.
"""
from isobox import CUBE, SLAB, STAIRS

# Tints the engine bakes into world geometry, as RGB. Source values are ABGR.
GRASS  = (0x5A, 0xCB, 0x6B)
OAK    = (0x48, 0xB5, 0x18)
SPRUCE = (0x3D, 0xAE, 0x2B)
BIRCH  = (0x80, 0xA7, 0x55)

SNOW_LAYER = (0.0, 0.0, 0.0, 1.0, 0.125, 1.0)


def cube(top, left=None, right=None, tint=None, top_tint=None, shape=CUBE):
    left = left or top
    return {"top": top, "left": left, "right": right or left,
            "tint": tint, "top_tint": top_tint, "shape": shape}


ICONS = {
    0:  cube("cobblestone"),
    1:  cube("stone_bricks"),
    2:  cube("mossy_stone_bricks"),
    3:  cube("cracked_stone_bricks"),
    4:  cube("mossy_cobblestone"),
    5:  cube("oak_planks"),
    6:  cube("bricks"),
    7:  cube("stone"),
    8:  cube("dirt"),
    9:  cube("grass_block_top", "grass_block_side", top_tint=GRASS),
    10: cube("clay"),
    11: cube("sandstone_top", "sandstone"),
    12: cube("sandstone_top", "chiseled_sandstone"),
    13: cube("sandstone_top", "cut_sandstone"),
    14: cube("sand"),
    15: cube("gravel"),
    16: cube("oak_log_top", "oak_log"),
    17: cube("oak_log_top", "spruce_log"),
    18: cube("oak_log_top", "birch_log"),
    19: cube("nether_bricks"),
    20: cube("netherrack"),

    21: cube("cobblestone", shape=STAIRS),
    22: cube("oak_planks", shape=STAIRS),
    23: cube("bricks", shape=STAIRS),
    24: cube("sandstone_top", "sandstone", shape=STAIRS),
    25: cube("stone_bricks", shape=STAIRS),
    26: cube("nether_bricks", shape=STAIRS),
    27: cube("quartz_block_top", "quartz_block_side", shape=STAIRS),

    28: cube("smooth_stone", "smooth_stone_slab_side", shape=SLAB),
    29: cube("cobblestone", shape=SLAB),
    30: cube("oak_planks", shape=SLAB),
    31: cube("bricks", shape=SLAB),
    32: cube("sandstone_top", "sandstone", shape=SLAB),
    33: cube("stone_bricks", shape=SLAB),
    34: cube("quartz_block_top", "quartz_block_side", shape=SLAB),

    35: cube("quartz_block_top", "quartz_block_side"),
    36: cube("quartz_pillar_top", "quartz_pillar_side"),
    37: cube("chiseled_quartz_block_top", "chiseled_quartz_block"),
    38: cube("coal_ore"),
    39: cube("iron_ore"),
    40: cube("gold_ore"),
    41: cube("emerald_ore"),
    42: cube("lapis_ore"),
    43: cube("redstone_ore"),
    44: cube("gold_block"),
    45: cube("iron_block"),
    46: cube("diamond_block"),
    47: cube("lapis_block"),
    48: cube("obsidian"),
    50: cube("ice"),
    51: cube("snow"),
    52: cube("snow", shape=SNOW_LAYER),
    53: cube("glass"),
    54: cube("glowstone"),
    55: cube("soul_sand"),
    56: cube("white_wool"),          # tinted per colour at draw time
    60: cube("oak_planks", "bookshelf"),
    61: cube("crafting_table_top", "crafting_table_front", "crafting_table_side"),
    62: cube("stonecutter_top", "stonecutter_bottom"),  # the real side art is cut out under the table
    64: cube("furnace_top", "furnace_front", "furnace_side"),
    65: cube("tnt_top", "tnt_side"),
    66: cube("cactus_top", "cactus_side"),
    67: cube("melon_top", "melon_side"),
    68: cube("oak_leaves", tint=OAK),
    69: cube("spruce_leaves", tint=SPRUCE),
    70: cube("birch_leaves", tint=BIRCH),
}

# 49 glowing obsidian and 55 nether reactor are MCPE-only. 57 trapdoor, 58 fence,
# 59 fence gate and 63 chest are shapes this renderer does not model.
