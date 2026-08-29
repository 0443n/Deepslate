//! Port of src/world/level/levelgen/features_common.cpp.

use crate::blocks::{self, WORLD_H};
use crate::random::Random;
use crate::world::World;

pub fn set_block<W: World>(w: &mut W, x: i32, y: i32, z: i32, id: u8, data: u8) {
    if y < 0 || y >= WORLD_H || !w.ready(x, z) {
        return;
    }
    w.set_block_and_data(x, y, z, id, data);
    if id == blocks::WATER || id == blocks::LAVA {
        let delay = if id == blocks::WATER { 5 } else { 30 };
        w.schedule_tick(x, y, z, id, delay);
    }
}

pub fn is_solid_gen(id: u8) -> bool {
    id != blocks::AIR
        && !blocks::is_water_id(id)
        && !blocks::is_leaf(id)
        && id != blocks::FLOWER
        && id != blocks::ROSE
        && id != blocks::MUSHROOM_BROWN
        && id != blocks::MUSHROOM_RED
}

pub fn heightmap_at<W: World>(w: &W, x: i32, z: i32) -> i32 {
    for y in (0..WORLD_H).rev() {
        let b = w.block(x, y, z);
        if b == blocks::GRASS
            || b == blocks::DIRT
            || b == blocks::SAND
            || b == blocks::STONE
            || b == blocks::GRAVEL
            || b == blocks::SANDSTONE
            || b == blocks::CLAY
            || b == blocks::BEDROCK
        {
            return y + 1;
        }
    }
    0
}

pub fn is_tree_clear(b: u8) -> bool {
    b == blocks::AIR || blocks::is_leaf(b)
}

/// `radius_at` takes (layer, tree_height, arg), matching the C++ function
/// pointer the tree features hand in.
pub fn tree_space_clear<W: World>(
    w: &W,
    x: i32,
    y: i32,
    z: i32,
    tree_height: i32,
    radius_at: fn(i32, i32, i32) -> i32,
    arg: i32,
) -> bool {
    if y < 1 || y + tree_height + 1 > WORLD_H {
        return false;
    }
    for yy in y..=y + 1 + tree_height {
        let r = radius_at(yy - y, tree_height, arg);
        for xx in x - r..=x + r {
            for zz in z - r..=z + r {
                if !is_tree_clear(w.block(xx, yy, zz)) {
                    return false;
                }
            }
        }
    }
    let below = w.block(x, y - 1, z);
    if below != blocks::GRASS && below != blocks::DIRT {
        return false;
    }
    true
}

fn basic_radius_at(layer: i32, tree_height: i32, _arg: i32) -> i32 {
    if layer == 0 {
        return 0;
    }
    if layer >= 1 + tree_height - 2 {
        2
    } else {
        1
    }
}

pub fn tree_basic<W: World>(
    w: &mut W,
    random: &mut Random,
    x: i32,
    y: i32,
    z: i32,
    min_height: i32,
    leaf_data: u8,
    log_data: u8,
) {
    let tree_height = random.next_int_bound(3) + min_height;
    if !tree_space_clear(w, x, y, z, tree_height, basic_radius_at, 0) {
        return;
    }
    set_block(w, x, y - 1, z, blocks::DIRT, 0);
    for yy in y - 3 + tree_height..=y + tree_height {
        let yo = yy - (y + tree_height);
        // Truncating division, so yo of -1 still gives an offset of 1.
        let offs = 1 - yo / 2;
        for xx in x - offs..=x + offs {
            let axo = (xx - x).abs();
            for zz in z - offs..=z + offs {
                let azo = (zz - z).abs();
                if axo == offs && azo == offs && (random.next_int_bound(2) == 0 || yo == 0) {
                    continue;
                }
                if !is_solid_gen(w.block(xx, yy, zz)) {
                    set_block(w, xx, yy, zz, blocks::LEAVES, leaf_data);
                }
            }
        }
    }
    for hh in 0..tree_height {
        let t = w.block(x, y + hh, z);
        if !is_solid_gen(t) {
            set_block(w, x, y + hh, z, blocks::LOG, log_data);
        }
    }
}
