//! Port of src/world/level/levelgen/feature_reeds.cpp.

use super::common::set_block;
use crate::blocks::{self, is_water_id};
use crate::random::Random;
use crate::world::World;

pub fn reeds_feature<W: World>(w: &mut W, random: &mut Random, x: i32, y: i32, z: i32) {
    for _ in 0..20 {
        let x2 = x + random.next_int_bound(4) - random.next_int_bound(4);
        let y2 = y;
        let z2 = z + random.next_int_bound(4) - random.next_int_bound(4);
        if w.block(x2, y2, z2) == blocks::AIR
            && (is_water_id(w.block(x2 - 1, y2 - 1, z2))
                || is_water_id(w.block(x2 + 1, y2 - 1, z2))
                || is_water_id(w.block(x2, y2 - 1, z2 - 1))
                || is_water_id(w.block(x2, y2 - 1, z2 + 1)))
        {
            // The inner draw comes first, as it does as a C++ argument.
            let bound = random.next_int_bound(3) + 1;
            let h = 2 + random.next_int_bound(bound);
            for yy in 0..h {
                let below = w.block(x2, y2 + yy - 1, z2);
                if below == blocks::REEDS
                    || below == blocks::GRASS
                    || below == blocks::DIRT
                    || below == blocks::SAND
                {
                    set_block(w, x2, y2 + yy, z2, blocks::REEDS, 0);
                } else {
                    break;
                }
            }
        }
    }
}
