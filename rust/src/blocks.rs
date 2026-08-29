//! Block ids and the pure predicates levelgen tests them with, mirroring the
//! subset of src/world/level/chunk/chunk.h the generator touches.

pub const AIR: u8 = 0;
pub const STONE: u8 = 1;
pub const GRASS: u8 = 2;
pub const DIRT: u8 = 3;
pub const SAPLING: u8 = 6;
pub const BEDROCK: u8 = 7;
pub const WATER: u8 = 8;
pub const CALM_WATER: u8 = 9;
pub const LAVA: u8 = 10;
pub const CALM_LAVA: u8 = 11;
pub const SAND: u8 = 12;
pub const GRAVEL: u8 = 13;
pub const LOG: u8 = 17;
pub const LEAVES: u8 = 18;
pub const SANDSTONE: u8 = 24;
pub const COBWEB: u8 = 30;
pub const TALLGRASS: u8 = 31;
pub const FLOWER: u8 = 37;
pub const ROSE: u8 = 38;
pub const MUSHROOM_BROWN: u8 = 39;
pub const MUSHROOM_RED: u8 = 40;
pub const FIRE: u8 = 51;
pub const WHEAT: u8 = 59;
pub const TOPSNOW: u8 = 78;
pub const ICE: u8 = 79;
pub const CACTUS: u8 = 81;
pub const CLAY: u8 = 82;
pub const REEDS: u8 = 83;
pub const NETHERRACK: u8 = 87;
pub const SOUL_SAND: u8 = 88;
pub const GLOWSTONE: u8 = 89;
pub const MELON_STEM: u8 = 105;
pub const INVISIBLE_BEDROCK: u8 = 95;

pub const LOG_OAK: u8 = 0;
pub const LOG_SPRUCE: u8 = 1;
pub const LOG_BIRCH: u8 = 2;

pub const LEAF_OAK: u8 = 0;
pub const LEAF_SPRUCE: u8 = 1;
pub const LEAF_BIRCH: u8 = 2;

pub const WORLD_CHUNKS_X: i32 = 16;
pub const WORLD_CHUNKS_Z: i32 = 16;
pub const WORLD_W: i32 = WORLD_CHUNKS_X * 16;
pub const WORLD_H: i32 = 128;
pub const WORLD_D: i32 = WORLD_CHUNKS_Z * 16;

pub fn is_water_id(id: u8) -> bool {
    id == WATER || id == CALM_WATER
}

pub fn is_lava_id(id: u8) -> bool {
    id == LAVA || id == CALM_LAVA
}

pub fn is_liquid_id(id: u8) -> bool {
    is_water_id(id) || is_lava_id(id)
}

pub fn is_leaf(id: u8) -> bool {
    id == LEAVES
}
