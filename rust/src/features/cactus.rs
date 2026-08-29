//! Port of src/world/level/levelgen/feature_cactus.cpp.

use super::common::{is_solid_gen, set_block};
use crate::blocks;
use crate::random::Random;
use crate::world::World;

fn cactus_can_survive<W: World>(w: &W, x: i32, y: i32, z: i32) -> bool {
    if is_solid_gen(w.block(x - 1, y, z)) {
        return false;
    }
    if is_solid_gen(w.block(x + 1, y, z)) {
        return false;
    }
    if is_solid_gen(w.block(x, y, z - 1)) {
        return false;
    }
    if is_solid_gen(w.block(x, y, z + 1)) {
        return false;
    }
    let below = w.block(x, y - 1, z);
    below == blocks::SAND || below == blocks::CACTUS
}

pub fn cactus_feature<W: World>(w: &mut W, random: &mut Random, x: i32, y: i32, z: i32) {
    for _ in 0..10 {
        let x2 = x + random.next_int_bound(8) - random.next_int_bound(8);
        let y2 = y + random.next_int_bound(4) - random.next_int_bound(4);
        let z2 = z + random.next_int_bound(8) - random.next_int_bound(8);
        if w.block(x2, y2, z2) == blocks::AIR {
            // The inner draw comes first, as it does as a C++ argument.
            let bound = random.next_int_bound(3) + 1;
            let h = 1 + random.next_int_bound(bound);
            for yy in 0..h {
                if cactus_can_survive(w, x2, y2 + yy, z2) {
                    set_block(w, x2, y2 + yy, z2, blocks::CACTUS, 0);
                }
            }
        }
    }
}
