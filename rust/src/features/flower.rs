//! Port of src/world/level/levelgen/feature_flower.cpp.

use super::common::set_block;
use super::pending::{PendingList, PendingSpot};
use crate::blocks;
use crate::random::Random;
use crate::world::World;

pub fn flower_feature<W: World>(
    w: &W,
    random: &mut Random,
    pending: &mut PendingList,
    x: i32,
    y: i32,
    z: i32,
    tile: u8,
) {
    for _ in 0..64 {
        let x2 = x + random.next_int_bound(8) - random.next_int_bound(8);
        let y2 = y + random.next_int_bound(4) - random.next_int_bound(4);
        let z2 = z + random.next_int_bound(8) - random.next_int_bound(8);
        if w.block(x2, y2, z2) == blocks::AIR {
            let below = w.block(x2, y2 - 1, z2);
            if below == blocks::GRASS || below == blocks::DIRT {
                pending.push(PendingSpot {
                    x: x2,
                    y: y2,
                    z: z2,
                    tile,
                });
            }
        }
    }
}

pub fn world_place_flowers<W: World>(w: &mut W, pending: &mut PendingList) {
    for i in 0..pending.as_slice().len() {
        let f = pending.as_slice()[i];

        let here = w.block(f.x, f.y, f.z);
        if here != blocks::AIR && here != blocks::TOPSNOW {
            continue;
        }
        let below = w.block(f.x, f.y - 1, f.z);
        if below != blocks::GRASS && below != blocks::DIRT {
            continue;
        }
        if w.light_raw(f.x, f.y, f.z) >= 8 || w.can_see_sky(f.x, f.y, f.z) {
            set_block(w, f.x, f.y, f.z, f.tile, 0);
        }
    }
    pending.clear();
}
