//! Port of src/world/level/levelgen/feature_snow.cpp.

use super::common::{heightmap_at, set_block};
use crate::blocks::{self, is_water_id, WORLD_H};
use crate::world::World;

pub fn snow_cap<W: World>(w: &mut W, chunk_x: i32, chunk_z: i32, m_temp: &[f32; 16 * 16]) {
    const SNOW_CUTOFF: f32 = 0.5;
    const SNOW_SCALE: f32 = 0.3;
    for lx in 0..16 {
        for lz in 0..16 {
            let gx = chunk_x * 16 + lx;
            let gz = chunk_z * 16 + lz;
            let y = heightmap_at(w, gx, gz);
            if y <= 0 || y >= WORLD_H {
                continue;
            }
            let temp = m_temp[(lx * 16 + lz) as usize] - (y - 64) as f32 / 64.0 * SNOW_SCALE;
            if temp >= SNOW_CUTOFF {
                continue;
            }
            if w.block(gx, y, gz) != blocks::AIR {
                continue;
            }
            let below = w.block(gx, y - 1, gz);
            if below == blocks::AIR || is_water_id(below) || below == blocks::ICE {
                continue;
            }
            set_block(w, gx, y, gz, blocks::TOPSNOW, 0);
        }
    }
}
