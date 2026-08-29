//! Port of src/world/level/levelgen/feature_lake.cpp.

use super::common::{is_solid_gen, set_block};
use crate::blocks::{self, is_lava_id, is_liquid_id};
use crate::random::Random;
use crate::world::World;

const SIZE: usize = 16 * 16 * 8;

fn at(xx: i32, zz: i32, yy: i32) -> usize {
    ((xx * 16 + zz) * 8 + yy) as usize
}

pub fn lake_feature<W: World>(
    w: &mut W,
    random: &mut Random,
    x: i32,
    y: i32,
    z: i32,
    tile: u8,
) {
    let x = x - 8;
    let z = z - 8;
    let mut y = y;
    while y > 0 && w.block(x, y, z) == blocks::AIR {
        y -= 1;
    }
    let y = y - 4;

    let mut grid = [false; SIZE];

    let spots = random.next_int_bound(4) + 4;
    for _ in 0..spots {
        let xr = random.next_float() * 6.0 + 3.0;
        let yr = random.next_float() * 4.0 + 2.0;
        let zr = random.next_float() * 6.0 + 3.0;

        let xp = random.next_float() * (16.0 - xr - 2.0) + 1.0 + xr / 2.0;
        let yp = random.next_float() * (8.0 - yr - 4.0) + 2.0 + yr / 2.0;
        let zp = random.next_float() * (16.0 - zr - 2.0) + 1.0 + zr / 2.0;

        for xx in 1..15 {
            for zz in 1..15 {
                for yy in 1..7 {
                    let xd = (xx as f32 - xp) / (xr / 2.0);
                    let yd = (yy as f32 - yp) / (yr / 2.0);
                    let zd = (zz as f32 - zp) / (zr / 2.0);
                    let d = xd * xd + yd * yd + zd * zd;
                    if d < 1.0 {
                        grid[at(xx, zz, yy)] = true;
                    }
                }
            }
        }
    }

    // A shell cell is an empty cell with a filled neighbour, so the lake only
    // lands where its whole rim sits in the right material.
    let shell = |grid: &[bool; SIZE], xx: i32, zz: i32, yy: i32| {
        !grid[at(xx, zz, yy)]
            && ((xx < 15 && grid[at(xx + 1, zz, yy)])
                || (xx > 0 && grid[at(xx - 1, zz, yy)])
                || (zz < 15 && grid[at(xx, zz + 1, yy)])
                || (zz > 0 && grid[at(xx, zz - 1, yy)])
                || (yy < 7 && grid[at(xx, zz, yy + 1)])
                || (yy > 0 && grid[at(xx, zz, yy - 1)]))
    };

    for xx in 0..16 {
        for zz in 0..16 {
            for yy in 0..8 {
                if shell(&grid, xx, zz, yy) {
                    let m = w.block(x + xx, y + yy, z + zz);
                    if yy >= 4 && is_liquid_id(m) {
                        return;
                    }
                    if yy < 4 && !is_solid_gen(m) && m != tile {
                        return;
                    }
                }
            }
        }
    }

    for xx in 0..16 {
        for zz in 0..16 {
            for yy in 0..8 {
                if grid[at(xx, zz, yy)] {
                    let id = if yy >= 4 { blocks::AIR } else { tile };
                    set_block(w, x + xx, y + yy, z + zz, id, 0);
                }
            }
        }
    }

    for xx in 0..16 {
        for zz in 0..16 {
            for yy in 4..8 {
                if grid[at(xx, zz, yy)]
                    && w.block(x + xx, y + yy - 1, z + zz) == blocks::DIRT
                    && w.can_see_sky(x + xx, y + yy, z + zz)
                {
                    set_block(w, x + xx, y + yy - 1, z + zz, blocks::GRASS, 0);
                }
            }
        }
    }

    if is_lava_id(tile) {
        for xx in 0..16 {
            for zz in 0..16 {
                for yy in 0..8 {
                    if shell(&grid, xx, zz, yy)
                        && (yy < 4 || random.next_int_bound(2) != 0)
                        && is_solid_gen(w.block(x + xx, y + yy, z + zz))
                    {
                        set_block(w, x + xx, y + yy, z + zz, blocks::STONE, 0);
                    }
                }
            }
        }
    }
}
