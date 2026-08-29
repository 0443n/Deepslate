"""Flat 16x16 icon slots in gui_blocks.png, mapped to modern textures.

Slot numbers are not written out here. They are read from kItemIcon in
src/gpu/item_icons.h and the id enum in src/world/item/item.h at generation time,
so this table only has to say which texture each item wants. A "block:" prefix
selects textures/block instead of textures/item.
"""

ITEMS = {
    "ITEM_SHOVEL_IRON": "iron_shovel",        "ITEM_PICKAXE_IRON": "iron_pickaxe",
    "ITEM_HATCHET_IRON": "iron_axe",          "ITEM_HOE_IRON": "iron_hoe",
    "ITEM_SWORD_IRON": "iron_sword",
    "ITEM_SHOVEL_WOOD": "wooden_shovel",      "ITEM_PICKAXE_WOOD": "wooden_pickaxe",
    "ITEM_HATCHET_WOOD": "wooden_axe",        "ITEM_HOE_WOOD": "wooden_hoe",
    "ITEM_SWORD_WOOD": "wooden_sword",
    "ITEM_SHOVEL_STONE": "stone_shovel",      "ITEM_PICKAXE_STONE": "stone_pickaxe",
    "ITEM_HATCHET_STONE": "stone_axe",        "ITEM_HOE_STONE": "stone_hoe",
    "ITEM_SWORD_STONE": "stone_sword",
    "ITEM_SHOVEL_DIAMOND": "diamond_shovel",  "ITEM_PICKAXE_DIAMOND": "diamond_pickaxe",
    "ITEM_HATCHET_DIAMOND": "diamond_axe",    "ITEM_HOE_DIAMOND": "diamond_hoe",
    "ITEM_SWORD_DIAMOND": "diamond_sword",
    "ITEM_SHOVEL_GOLD": "golden_shovel",      "ITEM_PICKAXE_GOLD": "golden_pickaxe",
    "ITEM_HATCHET_GOLD": "golden_axe",        "ITEM_HOE_GOLD": "golden_hoe",
    "ITEM_SWORD_GOLD": "golden_sword",

    "ITEM_HELMET_CLOTH": "leather_helmet",    "ITEM_CHESTPLATE_CLOTH": "leather_chestplate",
    "ITEM_LEGGINGS_CLOTH": "leather_leggings", "ITEM_BOOTS_CLOTH": "leather_boots",
    "ITEM_HELMET_IRON": "iron_helmet",        "ITEM_CHESTPLATE_IRON": "iron_chestplate",
    "ITEM_LEGGINGS_IRON": "iron_leggings",    "ITEM_BOOTS_IRON": "iron_boots",
    "ITEM_HELMET_DIAMOND": "diamond_helmet",  "ITEM_CHESTPLATE_DIAMOND": "diamond_chestplate",
    "ITEM_LEGGINGS_DIAMOND": "diamond_leggings", "ITEM_BOOTS_DIAMOND": "diamond_boots",
    "ITEM_HELMET_GOLD": "golden_helmet",      "ITEM_CHESTPLATE_GOLD": "golden_chestplate",
    "ITEM_LEGGINGS_GOLD": "golden_leggings",  "ITEM_BOOTS_GOLD": "golden_boots",

    "ITEM_BOW": "bow",                        "ITEM_ARROW": "arrow",
    "ITEM_SHEARS": "shears",                  "ITEM_FLINT_AND_STEEL": "flint_and_steel",
    "ITEM_FLINT": "flint",                    "ITEM_STICK": "stick",
    "ITEM_STRING": "string",                  "ITEM_FEATHER": "feather",
    "ITEM_GUNPOWDER": "gunpowder",            "ITEM_LEATHER": "leather",
    "ITEM_DIAMOND": "diamond",                "ITEM_IRON_INGOT": "iron_ingot",
    "ITEM_GOLD_INGOT": "gold_ingot",          "ITEM_BRICK": "brick",
    "ITEM_CLAY": "clay_ball",                 "ITEM_PAPER": "paper",
    "ITEM_BOOK": "book",                      "ITEM_BONE": "bone",
    "ITEM_SUGAR": "sugar",                    "ITEM_SNOWBALL": "snowball",
    "ITEM_GLOWSTONE_DUST": "glowstone_dust",  "ITEM_EGG": "egg",
    "ITEM_NETHER_BRICK": "nether_brick",      "ITEM_NETHER_QUARTZ": "quartz",

    "ITEM_APPLE": "apple",                    "ITEM_BREAD": "bread",
    "ITEM_WHEAT": "wheat",                    "ITEM_SEEDS_WHEAT": "wheat_seeds",
    "ITEM_SEEDS_MELON": "melon_seeds",        "ITEM_MELON": "melon_slice",
    "ITEM_BOWL": "bowl",                      "ITEM_MUSHROOM_STEW": "mushroom_stew",
    "ITEM_PORKCHOP_RAW": "porkchop",          "ITEM_PORKCHOP_COOKED": "cooked_porkchop",
    "ITEM_BEEF_RAW": "beef",                  "ITEM_BEEF_COOKED": "cooked_beef",
    "ITEM_CHICKEN_RAW": "chicken",            "ITEM_CHICKEN_COOKED": "cooked_chicken",
    "ITEM_CAKE": "cake",

    "ITEM_SIGN": "oak_sign",                  "ITEM_DOOR_WOOD_ITEM": "oak_door",
    "ITEM_PAINTING": "painting",
    "ITEM_REEDS": "sugar_cane",
}

# Slots reached through the II_* constants rather than kItemIcon.
SPECIAL = {
    "II_HELMET_CHAIN": "chainmail_helmet",    "II_CHESTPLATE_CHAIN": "chainmail_chestplate",
    "II_LEGGINGS_CHAIN": "chainmail_leggings", "II_BOOTS_CHAIN": "chainmail_boots",
    "II_BOW_PULL_0": "bow_pulling_0",         "II_BOW_PULL_1": "bow_pulling_1",
    "II_BOW_PULL_2": "bow_pulling_2",
    "II_BUCKET_EMPTY": "bucket",              "II_BUCKET_WATER": "water_bucket",
    "II_BUCKET_LAVA": "lava_bucket",          "II_BUCKET_MILK": "milk_bucket",
    "II_EGG": "egg",
    # Kept as shipped: II_CAKE (MCPE-only) and the spawn egg pair,
    # which MCPSP tints per mob while modern vanilla ships one egg per mob already.
}

# Coal slots are kItemIconCoal, indexed by data value.
COAL = {0: "coal", 1: "charcoal"}

# Dye slots are kItemIconDye, indexed by data value in the legacy dye order.
DYE = {
    1: "red_dye", 2: "green_dye", 4: "lapis_lazuli", 5: "purple_dye",
    6: "cyan_dye", 9: "pink_dye", 10: "lime_dye", 11: "yellow_dye",
    12: "light_blue_dye", 13: "magenta_dye", 14: "orange_dye", 15: "bone_meal",
}

# Flat icons for blocks, drawn from the block textures rather than item art.
BLOCK_FLAT = {
    # Keys are getGuiBlockIcon slots minus 128, since drawBlockIcon rebases them
    # onto the flat grid. Door 3 and reeds 10 already come from ITEMS.
    6: "block:dandelion", 7: "block:poppy",
    8: "block:brown_mushroom", 9: "block:red_mushroom",
    11: "block:oak_sapling", 12: "block:spruce_sapling", 13: "block:birch_sapling",
    # Kept as shipped: 2 glass pane and 4 bed, neither of which has a flat
    # modern counterpart.
}
