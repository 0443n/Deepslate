//! Port of src/world/level/levelgen/feature_mushroom.cpp.

use super::common::{is_solid_gen, set_block};
use super::pending::{PendingList, PendingSpot};
use crate::blocks;
use crate::random::Random;
use crate::world::World;

pub fn mushroom_feature<W: World>(
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
            if is_solid_gen(below) {
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

pub fn world_place_mushrooms<W: World>(w: &mut W, pending: &mut PendingList) {
    for i in 0..pending.as_slice().len() {
        let m = pending.as_slice()[i];
        if w.block(m.x, m.y, m.z) != blocks::AIR {
            continue;
        }
        if !is_solid_gen(w.block(m.x, m.y - 1, m.z)) {
            continue;
        }
        if w.light_raw(m.x, m.y, m.z) < 13 {
            set_block(w, m.x, m.y, m.z, m.tile, 0);
        }
    }
    pending.clear();
}
