//! Port of src/world/level/levelgen/feature_spring.cpp.

use super::common::set_block;
use crate::blocks;
use crate::world::World;

pub fn spring_feature<W: World>(w: &mut W, x: i32, y: i32, z: i32, tile: u8) {
    if w.block(x, y + 1, z) != blocks::STONE {
        return;
    }
    if w.block(x, y - 1, z) != blocks::STONE {
        return;
    }

    let current = w.block(x, y, z);
    if current != blocks::AIR && current != blocks::STONE {
        return;
    }

    let mut rock_count = 0;
    if w.block(x - 1, y, z) == blocks::STONE {
        rock_count += 1;
    }
    if w.block(x + 1, y, z) == blocks::STONE {
        rock_count += 1;
    }
    if w.block(x, y, z - 1) == blocks::STONE {
        rock_count += 1;
    }
    if w.block(x, y, z + 1) == blocks::STONE {
        rock_count += 1;
    }

    let mut hole_count = 0;
    if w.block(x - 1, y, z) == blocks::AIR {
        hole_count += 1;
    }
    if w.block(x + 1, y, z) == blocks::AIR {
        hole_count += 1;
    }
    if w.block(x, y, z - 1) == blocks::AIR {
        hole_count += 1;
    }
    if w.block(x, y, z + 1) == blocks::AIR {
        hole_count += 1;
    }

    if rock_count == 3 && hole_count == 1 {
        set_block(w, x, y, z, tile, 0);
    }
}
