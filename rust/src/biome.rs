//! Port of src/world/level/levelgen/biome.cpp. The McpeGen::computeBiome half
//! of that file belongs to the generator and lands with it.

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum BiomeId {
    Tundra = 0,
    Savanna,
    Desert,
    Swamp,
    Taiga,
    Shrub,
    Forest,
    Plains,
    Seasonal,
    Rain,
}

pub fn classify_biome(temperature: f32, downfall: f32) -> BiomeId {
    // Both inputs are quantised to 64 steps before anything is compared.
    let t = (temperature * 63.0) as i32 as f32 / 63.0;
    let d = (downfall * 63.0) as i32 as f32 / 63.0;
    let d = d * t;

    if t < 0.10 {
        return BiomeId::Tundra;
    }
    if d < 0.20 {
        if t < 0.50 {
            return BiomeId::Tundra;
        } else if t < 0.95 {
            return BiomeId::Savanna;
        } else {
            return BiomeId::Desert;
        }
    }
    if d > 0.5 && t < 0.7 {
        return BiomeId::Swamp;
    }
    if t < 0.50 {
        return BiomeId::Taiga;
    }
    if t < 0.97 {
        return if d < 0.35 {
            BiomeId::Shrub
        } else {
            BiomeId::Forest
        };
    }
    if d < 0.45 {
        BiomeId::Plains
    } else if d < 0.90 {
        BiomeId::Seasonal
    } else {
        BiomeId::Rain
    }
}

/// Returns the surface pair as (top, material).
pub fn biome_surface(b: BiomeId) -> (u8, u8) {
    use crate::blocks;
    if b == BiomeId::Desert {
        (blocks::SAND, blocks::SAND)
    } else {
        (blocks::GRASS, blocks::DIRT)
    }
}
