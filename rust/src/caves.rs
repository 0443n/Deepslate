//! Port of src/world/level/levelgen/caves.cpp.

use crate::blocks::{self, is_water_id, WORLD_H};
use crate::fdlibm::{cosf, sinf};
use crate::features::set_block;
use crate::random::Random;
use crate::world::World;

// Several draws here sit in expressions whose operand order C++ leaves
// unspecified, so the port fixes it left to right. psp-gcc and the host g++ the
// vectors come from were both checked to agree with that.
const MCPE_PI: f32 = 3.14159265;
const CAVE_LAVA_LEVEL: i32 = 10;
const CAVE_RARITY: i32 = 15;

fn floorf_i32(v: f32) -> i32 {
    crate::mth::floor(v)
}

// The C++ keeps each tunnel's Random on the heap to spare the PSP stack. Here
// it is a local, which is safe because the recursion is at most two deep, a
// split child always gets a thickness below 1 and so can never split again.
#[allow(clippy::too_many_arguments)]
fn cave_add_tunnel<W: World>(
    w: &mut W,
    parent_random: &mut Random,
    x_offs: i32,
    z_offs: i32,
    mut x_cave: f32,
    mut y_cave: f32,
    mut z_cave: f32,
    thickness: f32,
    mut y_rot: f32,
    mut x_rot: f32,
    mut step: i32,
    mut dist: i32,
    y_scale: f32,
) {
    let x_mid = (x_offs * 16 + 8) as f32;
    let z_mid = (z_offs * 16 + 8) as f32;

    let mut y_rota: f32 = 0.0;
    let mut x_rota: f32 = 0.0;
    let mut random = Random::new(parent_random.next_long());

    if dist <= 0 {
        let max_dist = 8 * 16 - 16;
        dist = max_dist - random.next_int_bound(max_dist / 4);
    }
    let mut single_step = false;
    if step == -1 {
        step = dist / 2;
        single_step = true;
    }

    let split_point = random.next_int_bound(dist / 2) + dist / 4;
    let steep = random.next_int_bound(6) == 0;

    while step < dist {
        let rad = 1.5 + sinf(step as f32 * MCPE_PI / dist as f32) * thickness;
        let y_rad = rad * y_scale;

        let xc = cosf(x_rot);
        let xs = sinf(x_rot);
        x_cave += cosf(y_rot) * xc;
        y_cave += xs;
        z_cave += sinf(y_rot) * xc;

        x_rot *= if steep { 0.92 } else { 0.7 };
        x_rot += x_rota * 0.1;
        y_rot += y_rota * 0.1;
        x_rota *= 0.90;
        y_rota *= 0.75;
        x_rota += (random.next_float() - random.next_float()) * random.next_float() * 2.0;
        y_rota += (random.next_float() - random.next_float()) * random.next_float() * 4.0;

        if !single_step && step == split_point && thickness > 1.0 {
            let t1 = random.next_float() * 0.5 + 0.5;
            cave_add_tunnel(
                w, &mut random, x_offs, z_offs, x_cave, y_cave, z_cave, t1,
                y_rot - MCPE_PI / 2.0, x_rot / 3.0, step, dist, 1.0,
            );
            let t2 = random.next_float() * 0.5 + 0.5;
            cave_add_tunnel(
                w, &mut random, x_offs, z_offs, x_cave, y_cave, z_cave, t2,
                y_rot + MCPE_PI / 2.0, x_rot / 3.0, step, dist, 1.0,
            );
            return;
        }
        if !single_step && random.next_int_bound(4) == 0 {
            step += 1;
            continue;
        }

        {
            let xd = x_cave - x_mid;
            let zd = z_cave - z_mid;
            let remaining = (dist - step) as f32;
            let rr = (thickness + 2.0) + 16.0;
            if xd * xd + zd * zd - (remaining * remaining) > rr * rr {
                return;
            }
        }

        if x_cave < x_mid - 16.0 - rad * 2.0
            || z_cave < z_mid - 16.0 - rad * 2.0
            || x_cave > x_mid + 16.0 + rad * 2.0
            || z_cave > z_mid + 16.0 + rad * 2.0
        {
            step += 1;
            continue;
        }

        let mut x0 = floorf_i32(x_cave - rad) - x_offs * 16 - 1;
        let mut x1 = floorf_i32(x_cave + rad) - x_offs * 16 + 1;
        let mut y0 = floorf_i32(y_cave - y_rad) - 1;
        let mut y1 = floorf_i32(y_cave + y_rad) + 1;
        let mut z0 = floorf_i32(z_cave - rad) - z_offs * 16 - 1;
        let mut z1 = floorf_i32(z_cave + rad) - z_offs * 16 + 1;

        if x0 < 0 {
            x0 = 0;
        }
        if x1 > 16 {
            x1 = 16;
        }
        if y0 < 1 {
            y0 = 1;
        }
        if y1 > 120 {
            y1 = 120;
        }
        if z0 < 0 {
            z0 = 0;
        }
        if z1 > 16 {
            z1 = 16;
        }

        // Walks the box looking for water, skipping the interior by snapping yy
        // down to the floor whenever the column is not on a face.
        let mut detected_water = false;
        let mut xx = x0;
        while !detected_water && xx < x1 {
            let mut zz = z0;
            while !detected_water && zz < z1 {
                let mut yy = y1 + 1;
                while !detected_water && yy >= y0 - 1 {
                    if yy >= 0
                        && yy < WORLD_H
                        && is_water_id(w.block(x_offs * 16 + xx, yy, z_offs * 16 + zz))
                    {
                        detected_water = true;
                    }
                    if yy != y0 - 1 && xx != x0 && xx != x1 - 1 && zz != z0 && zz != z1 - 1 {
                        yy = y0;
                    }
                    yy -= 1;
                }
                zz += 1;
            }
            xx += 1;
        }
        if detected_water {
            step += 1;
            continue;
        }

        for xx in x0..x1 {
            let xd = ((xx + x_offs * 16) as f32 + 0.5 - x_cave) / rad;
            for zz in z0..z1 {
                let zd = ((zz + z_offs * 16) as f32 + 0.5 - z_cave) / rad;
                if xd * xd + zd * zd >= 1.0 {
                    continue;
                }
                let mut has_grass = false;
                let gx = x_offs * 16 + xx;
                let gz = z_offs * 16 + zz;
                for yy in (y0..y1).rev() {
                    let yd = (yy as f32 + 0.5 - y_cave) / y_rad;
                    if yd > -0.7 && xd * xd + yd * yd + zd * zd < 1.0 {
                        let block = w.block(gx, yy, gz);
                        if block == blocks::GRASS {
                            has_grass = true;
                        }
                        if block == blocks::STONE
                            || block == blocks::DIRT
                            || block == blocks::GRASS
                        {
                            if yy < CAVE_LAVA_LEVEL {
                                set_block(w, gx, yy, gz, blocks::LAVA, 0);
                            } else {
                                set_block(w, gx, yy, gz, blocks::AIR, 0);
                                if has_grass && w.block(gx, yy - 1, gz) == blocks::DIRT {
                                    set_block(w, gx, yy - 1, gz, blocks::GRASS, 0);
                                }
                            }
                        }
                    }
                }
            }
        }
        if single_step {
            break;
        }
        step += 1;
    }
}

fn cave_add_room<W: World>(
    w: &mut W,
    random: &mut Random,
    x_offs: i32,
    z_offs: i32,
    x_room: f32,
    y_room: f32,
    z_room: f32,
) {
    let thickness = 1.0 + random.next_float() * 6.0;
    cave_add_tunnel(
        w, random, x_offs, z_offs, x_room, y_room, z_room, thickness, 0.0, 0.0, -1, -1, 0.5,
    );
}

fn cave_add_feature<W: World>(
    w: &mut W,
    random: &mut Random,
    x: i32,
    z: i32,
    x_offs: i32,
    z_offs: i32,
) {
    let inner = random.next_int_bound(40) + 1;
    let mid = random.next_int_bound(inner) + 1;
    let mut caves = random.next_int_bound(mid);
    if random.next_int_bound(CAVE_RARITY) != 0 {
        caves = 0;
    }

    for _ in 0..caves {
        let x_cave = (x * 16 + random.next_int_bound(16)) as f32;
        let y_bound = random.next_int_bound(120) + 8;
        let y_cave = random.next_int_bound(y_bound) as f32;
        let z_cave = (z * 16 + random.next_int_bound(16)) as f32;

        let mut tunnels = 1;
        if random.next_int_bound(4) == 0 {
            cave_add_room(w, random, x_offs, z_offs, x_cave, y_cave, z_cave);
            tunnels += random.next_int_bound(4);
        }
        for _ in 0..tunnels {
            let y_rot = random.next_float() * MCPE_PI * 2.0;
            let x_rot = ((random.next_float() - 0.5) * 2.0) / 8.0;
            let thickness = random.next_float() * 2.0 + random.next_float();
            cave_add_tunnel(
                w, random, x_offs, z_offs, x_cave, y_cave, z_cave, thickness, y_rot, x_rot, 0,
                0, 1.0,
            );
        }
    }
}

pub fn cave_feature<W: World>(w: &mut W, world_seed: i32, chunk_x: i32, chunk_z: i32) {
    let mut random = Random::new(world_seed);
    // long is 32 bits on the PSP, so every step of the seed mix wraps.
    let x_scale = (random.next_long() / 2).wrapping_mul(2).wrapping_add(1);
    let z_scale = (random.next_long() / 2).wrapping_mul(2).wrapping_add(1);

    const R: i32 = 8;
    for x in chunk_x - R..=chunk_x + R {
        for z in chunk_z - R..=chunk_z + R {
            let seed = (x.wrapping_mul(x_scale).wrapping_add(z.wrapping_mul(z_scale))) ^ world_seed;
            random.set_seed(seed);
            cave_add_feature(w, &mut random, x, z, chunk_x, chunk_z);
        }
    }
}
