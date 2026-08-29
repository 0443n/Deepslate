//! Port of src/world/level/levelgen/feature_clay.cpp.

use super::common::set_block;
use crate::blocks::{self, is_water_id};
use crate::fdlibm::{cosf, sinf};
use crate::mth::floor;
use crate::random::Random;
use crate::world::World;

const MCPE_PI: f32 = 3.14159265;

pub fn clay_feature<W: World>(w: &mut W, random: &mut Random, x: i32, y: i32, z: i32) {
    const COUNT: i32 = 32;
    if !is_water_id(w.block(x, y, z)) {
        return;
    }

    let dir = random.next_float() * MCPE_PI;
    // count / 8 is integer division here, unlike the ore feature next door.
    let x0 = (x + 8) as f32 + sinf(dir) * (COUNT / 8) as f32;
    let x1 = (x + 8) as f32 - sinf(dir) * (COUNT / 8) as f32;
    let z0 = (z + 8) as f32 + cosf(dir) * (COUNT / 8) as f32;
    let z1 = (z + 8) as f32 - cosf(dir) * (COUNT / 8) as f32;
    let y0 = (y + random.next_int_bound(3) + 2) as f32;
    let y1 = (y + random.next_int_bound(3) + 2) as f32;

    for d in 0..=COUNT {
        let xx = x0 + (x1 - x0) * d as f32 / COUNT as f32;
        let yy = y0 + (y1 - y0) * d as f32 / COUNT as f32;
        let zz = z0 + (z1 - z0) * d as f32 / COUNT as f32;
        let ss = random.next_float() * (COUNT >> 4) as f32;
        let r = (sinf(d as f32 * MCPE_PI / COUNT as f32) + 1.0) * ss + 1.0;

        for x2 in floor(xx - r / 2.0)..=floor(xx + r / 2.0) {
            for y2 in floor(yy - r / 2.0)..=floor(yy + r / 2.0) {
                for z2 in floor(zz - r / 2.0)..=floor(zz + r / 2.0) {
                    let xd = ((x2 as f32 + 0.5) - xx) / (r / 2.0);
                    let yd = ((y2 as f32 + 0.5) - yy) / (r / 2.0);
                    let zd = ((z2 as f32 + 0.5) - zz) / (r / 2.0);
                    if xd * xd + yd * yd + zd * zd < 1.0 && w.block(x2, y2, z2) == blocks::SAND {
                        set_block(w, x2, y2, z2, blocks::CLAY, 0);
                    }
                }
            }
        }
    }
}
