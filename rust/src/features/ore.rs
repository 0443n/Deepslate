//! Port of src/world/level/levelgen/feature_ore.cpp.

use super::common::set_block;
use crate::blocks;
use crate::fdlibm::{cosf, sinf};
use crate::mth::floor;
use crate::random::Random;
use crate::world::World;

// The generator's own pi, one digit short of Mth::PI.
const MCPE_PI: f32 = 3.14159265;

pub fn ore_feature<W: World>(
    w: &mut W,
    random: &mut Random,
    x: i32,
    y: i32,
    z: i32,
    tile: u8,
    count: i32,
) {
    let dir = random.next_float() * MCPE_PI;

    let x0 = (x + 8) as f32 + sinf(dir) * count as f32 / 8.0;
    let x1 = (x + 8) as f32 - sinf(dir) * count as f32 / 8.0;
    let z0 = (z + 8) as f32 + cosf(dir) * count as f32 / 8.0;
    let z1 = (z + 8) as f32 - cosf(dir) * count as f32 / 8.0;

    let y0 = (y + random.next_int_bound(3) + 2) as f32;
    let y1 = (y + random.next_int_bound(3) + 2) as f32;

    for cap_d in 0..=count {
        let d = cap_d as f32;
        let xx = x0 + (x1 - x0) * d / count as f32;
        let yy = y0 + (y1 - y0) * d / count as f32;
        let zz = z0 + (z1 - z0) * d / count as f32;

        let ss = random.next_float() * count as f32 / 16.0;
        let r = (sinf(d * MCPE_PI / count as f32) + 1.0) * ss + 1.0;
        let hr = (sinf(d * MCPE_PI / count as f32) + 1.0) * ss + 1.0;

        let xt0 = floor(xx - r / 2.0);
        let yt0 = floor(yy - hr / 2.0);
        let zt0 = floor(zz - r / 2.0);

        let xt1 = floor(xx + r / 2.0);
        let yt1 = floor(yy + hr / 2.0);
        let zt1 = floor(zz + r / 2.0);

        for x2 in xt0..=xt1 {
            let xd = ((x2 as f32 + 0.5) - xx) / (r / 2.0);
            if xd * xd < 1.0 {
                for y2 in yt0..=yt1 {
                    let yd = ((y2 as f32 + 0.5) - yy) / (hr / 2.0);
                    if xd * xd + yd * yd < 1.0 {
                        for z2 in zt0..=zt1 {
                            let zd = ((z2 as f32 + 0.5) - zz) / (r / 2.0);
                            if xd * xd + yd * yd + zd * zd < 1.0
                                && w.block(x2, y2, z2) == blocks::STONE
                            {
                                set_block(w, x2, y2, z2, tile, 0);
                            }
                        }
                    }
                }
            }
        }
    }
}
