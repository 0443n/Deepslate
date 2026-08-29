//! Port of src/world/level/levelgen/feature_tree_spruce.cpp.

use super::common::{is_solid_gen, set_block, tree_space_clear};
use crate::blocks;
use crate::random::Random;
use crate::world::World;

// The trunk height and leaf radius share one int so the shape fits the radius
// callback the C++ hands to treeSpaceClear.
fn cone_radius_at(layer: i32, _tree_height: i32, arg: i32) -> i32 {
    if layer < (arg >> 8) {
        0
    } else {
        arg & 0xFF
    }
}

pub fn tree_spruce<W: World>(w: &mut W, random: &mut Random, x: i32, y: i32, z: i32) {
    let tree_height = random.next_int_bound(4) + 6;
    let trunk_height = 1 + random.next_int_bound(2);
    let top_height = tree_height - trunk_height;
    let leaf_radius = 2 + random.next_int_bound(2);

    let arg = (trunk_height << 8) | leaf_radius;
    if !tree_space_clear(w, x, y, z, tree_height, cone_radius_at, arg) {
        return;
    }
    set_block(w, x, y - 1, z, blocks::DIRT, 0);

    let mut current_radius = random.next_int_bound(2);
    let mut max_radius = 1;
    let mut min_radius = 0;
    for height_pos in 0..=top_height {
        let yy = y + tree_height - height_pos;
        for xx in x - current_radius..=x + current_radius {
            let axo = (xx - x).abs();
            for zz in z - current_radius..=z + current_radius {
                let azo = (zz - z).abs();
                if axo == current_radius && azo == current_radius && current_radius > 0 {
                    continue;
                }
                if !is_solid_gen(w.block(xx, yy, zz)) {
                    set_block(w, xx, yy, zz, blocks::LEAVES, blocks::LEAF_SPRUCE);
                }
            }
        }
        if current_radius >= max_radius {
            current_radius = min_radius;
            min_radius = 1;
            max_radius += 1;
            if max_radius > leaf_radius {
                max_radius = leaf_radius;
            }
        } else {
            current_radius += 1;
        }
    }

    let top_offset = random.next_int_bound(3);
    for hh in 0..tree_height - top_offset {
        let t = w.block(x, y + hh, z);
        if !is_solid_gen(t) {
            set_block(w, x, y + hh, z, blocks::LOG, blocks::LOG_SPRUCE);
        }
    }
}
