# (col,row) -> modern vanilla block texture basename, or None to keep the
# legacy art. Cells are the ones MCPSP's Tile::getTexture actually addresses.
MAP = {
 (0,0):"grass_block_top", (1,0):"stone", (2,0):"dirt", (3,0):"grass_block_side",
 (4,0):"oak_planks", (5,0):"smooth_stone_slab_side", (6,0):"smooth_stone",
 (7,0):"bricks", (8,0):"tnt_side", (9,0):"tnt_top", (10,0):"tnt_bottom",
 (11,0):"cobweb", (12,0):"poppy", (13,0):"dandelion", (15,0):"oak_sapling",

 (0,1):"cobblestone", (1,1):"bedrock", (2,1):"sand", (3,1):"gravel",
 (4,1):"oak_log", (5,1):"oak_log_top", (6,1):"iron_block", (7,1):"gold_block",
 (8,1):"diamond_block", (9,1):None, (10,1):None, (11,1):None,
 (12,1):"red_mushroom", (13,1):"brown_mushroom", (15,1):"fire_0",

 (0,2):"gold_ore", (1,2):"iron_ore", (2,2):"coal_ore", (3,2):"bookshelf",
 (4,2):"mossy_cobblestone", (5,2):"obsidian", (7,2):"short_grass",
 (11,2):"crafting_table_top", (12,2):"furnace_front", (13,2):"furnace_side",

 (1,3):"glass", (2,3):"emerald_ore", (3,3):"redstone_ore", (4,3):"oak_leaves",
 (6,3):"stone_bricks", (7,3):"dead_bush", (8,3):"fern",
 (11,3):"crafting_table_front", (12,3):"crafting_table_side", (14,3):"furnace_top",
 (15,3):"spruce_sapling",

 (2,4):"snow", (3,4):"ice", (5,4):"cactus_top", (6,4):"cactus_side",
 (7,4):"cactus_bottom", (8,4):"clay", (9,4):"sugar_cane", (15,4):"birch_sapling",

 (0,5):"torch", (1,5):"oak_door_top", (2,5):"iron_door_top", (3,5):"ladder",
 (4,5):"oak_trapdoor", (6,5):"farmland_moist", (7,5):"farmland",

 (1,6):"oak_door_bottom", (2,6):"iron_door_bottom",
 (4,6):"mossy_stone_bricks", (5,6):"cracked_stone_bricks",
 (7,6):"netherrack", (8,6):"soul_sand", (9,6):"glowstone", (15,6):"melon_stem",

 (4,7):"spruce_log", (5,7):"birch_log",

 (4,8):"spruce_leaves", (6,8):None, (7,8):None,
 (8,8):"melon_side", (9,8):"melon_top",

 (0,9):"lapis_block", (5,9):None, (6,9):None, (7,9):None, (8,9):None,

 (0,10):"lapis_ore", (8,10):"stonecutter_side", (9,10):"stonecutter_top",

 (0,11):"sandstone_top",

 (0,12):"sandstone", (4,12):"quartz_block_top", (5,12):"quartz_pillar_top",
 (6,12):"chiseled_quartz_block_top", (13,12):"water_still", (14,12):"water_flow",

 (0,13):"sandstone_bottom", (3,13):"quartz_block_bottom", (4,13):"quartz_block_side",
 (5,13):"quartz_pillar_side", (6,13):"chiseled_quartz_block", (10,13):None,

 (0,14):"nether_bricks", (5,14):"chiseled_sandstone", (6,14):"cut_sandstone",
 (8,14):"nether_portal", (9,14):None, (10,14):None,
 (13,14):"lava_still", (14,14):"lava_flow",

 (12,15):None, (13,15):None,
}

# Wheat has eight growth stages laid out left to right on row 5.
for i in range(8):
    MAP[(8+i,5)] = "wheat_stage%d" % i

# Wool cells come from tileWool's bit shuffle, so mirror it rather than restate it.
_WOOL = ["white","orange","magenta","light_blue","yellow","lime","pink","gray",
         "light_gray","cyan","purple","blue","brown","green","red","black"]
for d in range(16):
    if d == 0:
        tex = 64
    else:
        di = (~d) & 0xF
        tex = 7*16+1 + ((di & 8) >> 3) + ((di & 7) * 16)
    MAP[(tex % 16, tex // 16)] = _WOOL[d] + "_wool"
