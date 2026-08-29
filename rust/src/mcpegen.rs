//! Port of src/world/level/levelgen/mcpegen.cpp and the McpeGen half of
//! biome.cpp. The window driver stays in C++, it is the piece that talks to the
//! chunk cache and yields to the scheduler.

extern crate alloc;
use alloc::boxed::Box;
use core::mem::MaybeUninit;
use core::ptr::{self, addr_of_mut};

use crate::biome::{biome_surface, classify_biome, BiomeId};
use crate::blocks::{self, WORLD_H};
use crate::caves::cave_feature;
use crate::features::{self, PendingList};
use crate::noise::PerlinNoise;
use crate::random::Random;
use crate::world::World;

const NCELL_W: i32 = 4;
const NCELL_H: i32 = 8;

const BIOME_ZOOM: f32 = 2.0;
const BIOME_TEMP_SCALE: f32 = BIOME_ZOOM / 80.0;
const BIOME_DOWN_SCALE: f32 = BIOME_ZOOM / 40.0;
const BIOME_NOISE_SCALE: f32 = 1.0 / 4.0;

// getHeights works a 5 x 17 x 5 lattice per chunk, which is what sizes every
// buffer the C++ grows on first use.
const HEIGHT_XZ: usize = 5;
const HEIGHT_Y: usize = 17;
const HEIGHT_REGION: usize = HEIGHT_XZ * HEIGHT_Y * HEIGHT_XZ;
const HEIGHT_FLAT: usize = HEIGHT_XZ * HEIGHT_XZ;

pub struct McpeGen {
    pub random: Random,
    rnd_temp: Random,
    rnd_downfall: Random,
    rnd_noise: Random,

    lperlin_noise1: PerlinNoise,
    lperlin_noise2: PerlinNoise,
    perlin_noise1: PerlinNoise,
    perlin_noise2: PerlinNoise,
    perlin_noise3: PerlinNoise,
    scale_noise: PerlinNoise,
    depth_noise: PerlinNoise,
    forest_noise: PerlinNoise,
    temperature_map: PerlinNoise,
    downfall_map: PerlinNoise,
    noise_map: PerlinNoise,

    buffer: [f32; 1024],
    pnr: [f32; HEIGHT_REGION],
    ar: [f32; HEIGHT_REGION],
    br: [f32; HEIGHT_REGION],
    sr: [f32; HEIGHT_FLAT],
    dr: [f32; HEIGHT_FLAT],
    raw_temp: [f32; 16 * 16],
    raw_downfall: [f32; 16 * 16],
    raw_noise: [f32; 16 * 16],
    pub m_temp: [f32; 16 * 16],
    m_downfall: [f32; 16 * 16],
    sand_buffer: [f32; 16 * 16],
    gravel_buffer: [f32; 16 * 16],
    depth_buffer: [f32; 16 * 16],

    world_seed: i32,
    m_phase_biome: i32,

    pub pending_flowers: PendingList,
    pub pending_mushrooms: PendingList,
}

impl McpeGen {
    /// Built in place because the noise tables and four Mersenne states do not
    /// fit on the terrain thread's stack. The C++ gets this for free from `new`.
    pub fn new(seed: i32) -> Box<McpeGen> {
        let mut b: Box<MaybeUninit<McpeGen>> = Box::new_uninit();
        unsafe {
            let p = b.as_mut_ptr();
            // Every buffer starts at zero, and each field below is written
            // before anything reads it.
            ptr::write_bytes(p, 0, 1);

            addr_of_mut!((*p).random).write(Random::new(seed));
            let s = seed as u32;
            addr_of_mut!((*p).rnd_temp).write(Random::new(s.wrapping_mul(9871) as i32));
            addr_of_mut!((*p).rnd_downfall).write(Random::new(s.wrapping_mul(39811) as i32));
            addr_of_mut!((*p).rnd_noise).write(Random::new(s.wrapping_mul(543321) as i32));

            // Declaration order, which is the order the shared Random is drawn.
            let r = &mut *addr_of_mut!((*p).random);
            addr_of_mut!((*p).lperlin_noise1).write(PerlinNoise::new(r, 16));
            addr_of_mut!((*p).lperlin_noise2).write(PerlinNoise::new(r, 16));
            addr_of_mut!((*p).perlin_noise1).write(PerlinNoise::new(r, 8));
            addr_of_mut!((*p).perlin_noise2).write(PerlinNoise::new(r, 4));
            addr_of_mut!((*p).perlin_noise3).write(PerlinNoise::new(r, 4));
            addr_of_mut!((*p).scale_noise).write(PerlinNoise::new(r, 10));
            addr_of_mut!((*p).depth_noise).write(PerlinNoise::new(r, 16));
            addr_of_mut!((*p).forest_noise).write(PerlinNoise::new(r, 8));

            let rt = &mut *addr_of_mut!((*p).rnd_temp);
            addr_of_mut!((*p).temperature_map).write(PerlinNoise::new(rt, 4));
            let rd = &mut *addr_of_mut!((*p).rnd_downfall);
            addr_of_mut!((*p).downfall_map).write(PerlinNoise::new(rd, 4));
            let rn = &mut *addr_of_mut!((*p).rnd_noise);
            addr_of_mut!((*p).noise_map).write(PerlinNoise::new(rn, 2));

            addr_of_mut!((*p).pending_flowers).write(PendingList::new());
            addr_of_mut!((*p).pending_mushrooms).write(PendingList::new());

            addr_of_mut!((*p).world_seed).write(seed);

            b.assume_init()
        }
    }

    pub fn compute_biome(&mut self, chunk_x: i32, chunk_z: i32) {
        let x = chunk_x * 16;
        let z = chunk_z * 16;
        self.temperature_map.get_region_flat(
            &mut self.raw_temp, x, z, 16, 16, BIOME_TEMP_SCALE, BIOME_TEMP_SCALE,
        );
        self.downfall_map.get_region_flat(
            &mut self.raw_downfall, x, z, 16, 16, BIOME_DOWN_SCALE, BIOME_DOWN_SCALE,
        );
        self.noise_map.get_region_flat(
            &mut self.raw_noise, x, z, 16, 16, BIOME_NOISE_SCALE, BIOME_NOISE_SCALE,
        );

        for pp in 0..16 * 16 {
            let noise = self.raw_noise[pp] * 1.1 + 0.5;

            let mut split2 = 0.01f32;
            let mut split1 = 1.0 - split2;
            let mut temperature = (self.raw_temp[pp] * 0.15 + 0.7) * split1 + noise * split2;
            split2 = 0.002;
            split1 = 1.0 - split2;
            let mut downfall = (self.raw_downfall[pp] * 0.15 + 0.5) * split1 + noise * split2;

            temperature = 1.0 - ((1.0 - temperature) * (1.0 - temperature));
            if temperature < 0.0 {
                temperature = 0.0;
            }
            if downfall < 0.0 {
                downfall = 0.0;
            }
            if temperature > 1.0 {
                temperature = 1.0;
            }
            if downfall > 1.0 {
                downfall = 1.0;
            }

            self.m_temp[pp] = temperature;
            self.m_downfall[pp] = downfall;
        }
    }

    fn get_heights(&mut self, x: i32, y: i32, z: i32, x_size: i32, y_size: i32, z_size: i32) {
        let s = 684.412f32;
        let hs = 684.412f32;

        self.scale_noise
            .get_region_flat(&mut self.sr, x, z, x_size, z_size, 1.121, 1.121);
        self.depth_noise
            .get_region_flat(&mut self.dr, x, z, x_size, z_size, 200.0, 200.0);

        self.perlin_noise1.get_region(
            &mut self.pnr, x as f32, y as f32, z as f32, x_size, y_size, z_size,
            s / 80.0, hs / 160.0, s / 80.0,
        );
        self.lperlin_noise1.get_region(
            &mut self.ar, x as f32, y as f32, z as f32, x_size, y_size, z_size, s, hs, s,
        );
        self.lperlin_noise2.get_region(
            &mut self.br, x as f32, y as f32, z as f32, x_size, y_size, z_size, s, hs, s,
        );

        let mut p = 0usize;
        let mut pp = 0usize;

        let w_scale = 16 / x_size;
        for xx in 0..x_size {
            let xp = xx * w_scale + w_scale / 2;
            for zz in 0..z_size {
                let zp = zz * w_scale + w_scale / 2;
                let temperature = self.m_temp[(xp * 16 + zp) as usize];
                let downfall = self.m_downfall[(xp * 16 + zp) as usize] * temperature;
                let mut dd = 1.0 - downfall;
                dd = dd * dd;
                dd = dd * dd;
                dd = 1.0 - dd;

                let mut scale = (self.sr[pp] + 256.0) / 512.0;
                scale *= dd;
                if scale > 1.0 {
                    scale = 1.0;
                }

                let mut depth = self.dr[pp] / 8000.0;
                if depth < 0.0 {
                    depth = -depth * 0.3;
                }
                depth = depth * 3.0 - 2.0;

                if depth < 0.0 {
                    depth /= 2.0;
                    if depth < -1.0 {
                        depth = -1.0;
                    }
                    depth /= 1.4;
                    depth /= 2.0;
                    scale = 0.0;
                } else {
                    if depth > 1.0 {
                        depth = 1.0;
                    }
                    depth /= 8.0;
                }

                if scale < 0.0 {
                    scale = 0.0;
                }
                scale += 0.5;
                depth = depth * y_size as f32 / 16.0;

                let y_center = y_size as f32 / 2.0 + depth * 4.0;

                pp += 1;

                for yy in 0..y_size {
                    let val;

                    let mut y_offs = (yy as f32 - y_center) * 12.0 / scale;
                    if y_offs < 0.0 {
                        y_offs *= 4.0;
                    }

                    let bb = self.ar[p] / 512.0;
                    let cc = self.br[p] / 512.0;

                    let v = (self.pnr[p] / 10.0 + 1.0) / 2.0;
                    if v < 0.0 {
                        val = bb;
                    } else if v > 1.0 {
                        val = cc;
                    } else {
                        val = bb + (cc - bb) * v;
                    }
                    let mut val = val - y_offs;

                    if yy > y_size - 4 {
                        let slide = (yy - (y_size - 4)) as f32 / (4.0 - 1.0);
                        val = val * (1.0 - slide) + -10.0 * slide;
                    }

                    self.buffer[p] = val;
                    p += 1;
                }
            }
        }
    }

    pub fn prepare_chunk<W: World>(&mut self, w: &mut W, chunk_x: i32, chunk_z: i32) {
        let x_chunks = 16 / NCELL_W;
        let x_size = x_chunks + 1;
        let y_size = 128 / NCELL_H + 1;
        let z_size = x_chunks + 1;

        self.get_heights(chunk_x * x_chunks, 0, chunk_z * x_chunks, x_size, y_size, z_size);

        let at = |xc: i32, zc: i32, yc: i32| ((xc * z_size + zc) * y_size + yc) as usize;

        for xc in 0..x_chunks {
            for zc in 0..x_chunks {
                for yc in 0..128 / NCELL_H {
                    let y_step = 1.0 / NCELL_H as f32;
                    let mut s0 = self.buffer[at(xc, zc, yc)];
                    let mut s1 = self.buffer[at(xc, zc + 1, yc)];
                    let mut s2 = self.buffer[at(xc + 1, zc, yc)];
                    let mut s3 = self.buffer[at(xc + 1, zc + 1, yc)];

                    let s0a = (self.buffer[at(xc, zc, yc + 1)] - s0) * y_step;
                    let s1a = (self.buffer[at(xc, zc + 1, yc + 1)] - s1) * y_step;
                    let s2a = (self.buffer[at(xc + 1, zc, yc + 1)] - s2) * y_step;
                    let s3a = (self.buffer[at(xc + 1, zc + 1, yc + 1)] - s3) * y_step;

                    for y in 0..NCELL_H {
                        let x_step = 1.0 / NCELL_W as f32;
                        let mut ss0 = s0;
                        let mut ss1 = s1;
                        let ss0a = (s2 - s0) * x_step;
                        let ss1a = (s3 - s1) * x_step;

                        for x in 0..NCELL_W {
                            let z_step = 1.0 / NCELL_W as f32;
                            let mut val = ss0;
                            let vala = (ss1 - ss0) * z_step;

                            for z in 0..NCELL_W {
                                let lx = xc * NCELL_W + x;
                                let lz = zc * NCELL_W + z;
                                let gx = chunk_x * 16 + lx;
                                let gz = chunk_z * 16 + lz;
                                let gy = yc * NCELL_H + y;

                                let temp = self.m_temp[(lx * 16 + lz) as usize];
                                let id = if val > 0.0 {
                                    blocks::STONE
                                } else if gy < 64 {
                                    if temp < 0.5 && gy >= 63 {
                                        blocks::ICE
                                    } else {
                                        blocks::CALM_WATER
                                    }
                                } else {
                                    blocks::AIR
                                };

                                w.put(gx, gy, gz, id);

                                val += vala;
                            }
                            ss0 += ss0a;
                            ss1 += ss1a;
                        }

                        s0 += s0a;
                        s1 += s1a;
                        s2 += s2a;
                        s3 += s3a;
                    }
                }
            }
        }
    }

    pub fn build_surfaces_chunk<W: World>(&mut self, w: &mut W, chunk_x: i32, chunk_z: i32) {
        const WATER_HEIGHT: i32 = 64;
        let x_offs = chunk_x;
        let z_offs = chunk_z;
        self.random.set_seed(
            x_offs
                .wrapping_mul(341872712)
                .wrapping_add(z_offs.wrapping_mul(132899541)),
        );
        let s = 1.0 / 32.0;
        self.perlin_noise2.get_region(
            &mut self.sand_buffer, (x_offs * 16) as f32, (z_offs * 16) as f32, 0.0,
            16, 16, 1, s, s, 1.0,
        );
        self.perlin_noise2.get_region(
            &mut self.gravel_buffer, (x_offs * 16) as f32, 109.01340, (z_offs * 16) as f32,
            16, 1, 16, s, 1.0, s,
        );
        self.perlin_noise3.get_region(
            &mut self.depth_buffer, (x_offs * 16) as f32, (z_offs * 16) as f32, 0.0,
            16, 16, 1, s * 2.0, s * 2.0, s * 2.0,
        );

        for x in 0..16 {
            for z in 0..16 {
                // Never assigned after this, so the ice branch below is dead in
                // the C++ too.
                let temp = 1.0f32;

                let biome = classify_biome(
                    self.m_temp[(z + x * 16) as usize],
                    self.m_downfall[(z + x * 16) as usize],
                );
                let (b_top, b_mat) = biome_surface(biome);

                let sand = (self.sand_buffer[(z + x * 16) as usize]
                    + self.random.next_float() * 0.2)
                    > 0.0;
                let gravel = (self.gravel_buffer[(z + x * 16) as usize]
                    + self.random.next_float() * 0.2)
                    > 3.0;
                let run_depth = (self.depth_buffer[(z + x * 16) as usize] / 3.0
                    + 3.0
                    + self.random.next_float() * 0.25) as i32;

                let mut run = -1;
                let mut top = b_top;
                let mut material = b_mat;

                let gx = chunk_x * 16 + x;
                let gz = chunk_z * 16 + z;

                let mut col = [0u8; WORLD_H as usize];
                w.column_get(gx, gz, &mut col);
                for y in (0..=127).rev() {
                    let cell = &mut col[y as usize];

                    if y <= self.random.next_int_bound(5) {
                        *cell = blocks::BEDROCK;
                    } else {
                        let old = *cell;
                        if old == blocks::AIR {
                            run = -1;
                        } else if old == blocks::STONE {
                            if run == -1 {
                                if run_depth <= 0 {
                                    top = blocks::AIR;
                                    material = blocks::STONE;
                                } else if y >= WATER_HEIGHT - 4 && y <= WATER_HEIGHT + 1 {
                                    top = b_top;
                                    material = b_mat;
                                    if gravel {
                                        top = blocks::AIR;
                                        material = blocks::GRAVEL;
                                    }
                                    if sand {
                                        top = blocks::SAND;
                                        material = blocks::SAND;
                                    }
                                }
                                if y < WATER_HEIGHT && top == blocks::AIR {
                                    top = if temp < 0.15 {
                                        blocks::ICE
                                    } else {
                                        blocks::CALM_WATER
                                    };
                                }
                                run = run_depth;
                                *cell = if y >= WATER_HEIGHT - 1 { top } else { material };
                            } else if run > 0 {
                                run -= 1;
                                *cell = material;
                                if run == 0 && material == blocks::SAND {
                                    run = self.random.next_int_bound(4);
                                    material = blocks::SANDSTONE;
                                }
                            }
                        }
                    }
                }
                w.column_put(gx, gz, &col);
            }
        }
    }

    /// True once the last phase has run, matching the C++ return.
    pub fn post_process_phase<W: World>(
        &mut self,
        w: &mut W,
        chunk_x: i32,
        chunk_z: i32,
        phase: i32,
    ) -> bool {
        let xo = chunk_x * 16;
        let zo = chunk_z * 16;
        match phase {
            0 => {
                self.compute_biome(chunk_x, chunk_z);
                self.m_phase_biome =
                    classify_biome(self.m_temp[8 * 16 + 8], self.m_downfall[8 * 16 + 8]) as i32;

                self.random.set_seed(self.world_seed);
                let x_scale = (self.random.next_int() / 2).wrapping_mul(2).wrapping_add(1);
                let z_scale = (self.random.next_int() / 2).wrapping_mul(2).wrapping_add(1);
                let h = (chunk_x as u32)
                    .wrapping_mul(x_scale as u32)
                    .wrapping_add((chunk_z as u32).wrapping_mul(z_scale as u32));
                self.random.set_seed((h ^ self.world_seed as u32) as i32);

                for _ in 0..10 {
                    let x = xo + self.random.next_int_bound(16);
                    let y = self.random.next_int_bound(128);
                    let z = zo + self.random.next_int_bound(16);
                    features::clay_feature(w, &mut self.random, x, y, z);
                }
                false
            }

            1 => {
                let ore = |w: &mut W, r: &mut Random, n: i32, y_bound: i32, tile: u8, count: i32| {
                    for _ in 0..n {
                        let x = xo + r.next_int_bound(16);
                        let y = r.next_int_bound(y_bound);
                        let z = zo + r.next_int_bound(16);
                        features::ore_feature(w, r, x, y, z, tile, count);
                    }
                };
                ore(w, &mut self.random, 20, 128, blocks::DIRT, 32);
                ore(w, &mut self.random, 10, 128, blocks::GRAVEL, 32);
                ore(w, &mut self.random, 20, 128, ORE_COAL, 16);
                ore(w, &mut self.random, 20, 64, ORE_IRON, 8);
                ore(w, &mut self.random, 2, 32, ORE_GOLD, 8);
                ore(w, &mut self.random, 8, 16, ORE_REDSTONE, 7);
                ore(w, &mut self.random, 1, 16, ORE_EMERALD, 7);
                // Lapis takes two draws for its height, so it does not fit the
                // shape above.
                for _ in 0..1 {
                    let x = xo + self.random.next_int_bound(16);
                    let y = self.random.next_int_bound(16) + self.random.next_int_bound(16);
                    let z = zo + self.random.next_int_bound(16);
                    features::ore_feature(w, &mut self.random, x, y, z, ORE_LAPIS, 6);
                }
                false
            }

            2 => {
                let biome = self.phase_biome();

                let fss = 0.5f32;
                let o_for = ((self
                    .forest_noise
                    .get_value2(xo as f32 * fss, zo as f32 * fss)
                    / 8.0
                    + self.random.next_float() * 4.0
                    + 4.0)
                    / 3.0) as i32;
                let mut forests = 0;
                if self.random.next_int_bound(10) == 0 {
                    forests += 1;
                }
                if biome == BiomeId::Forest {
                    forests += o_for + 2;
                }
                if biome == BiomeId::Rain {
                    forests += o_for + 2;
                }
                if biome == BiomeId::Seasonal {
                    forests += o_for + 1;
                }
                if biome == BiomeId::Taiga {
                    forests += o_for + 1;
                }
                if biome == BiomeId::Desert {
                    forests -= 20;
                }
                if biome == BiomeId::Tundra {
                    forests -= 20;
                }
                if biome == BiomeId::Plains {
                    forests -= 20;
                }
                for _ in 0..forests {
                    let tx = xo + self.random.next_int_bound(16) + 8;
                    let tz = zo + self.random.next_int_bound(16) + 8;
                    let ty = features::heightmap_at(w, tx, tz);

                    if biome == BiomeId::Taiga {
                        if self.random.next_int_bound(3) == 0 {
                            features::tree_pine(w, &mut self.random, tx, ty, tz);
                        } else {
                            features::tree_spruce(w, &mut self.random, tx, ty, tz);
                        }
                    } else if biome == BiomeId::Forest {
                        if self.random.next_int_bound(5) == 0 {
                            features::tree_birch(w, &mut self.random, tx, ty, tz);
                        } else {
                            // The discarded draw keeps the stream aligned with
                            // the branch above.
                            self.random.next_int_bound(3);
                            features::tree_oak(w, &mut self.random, tx, ty, tz);
                        }
                    } else if biome == BiomeId::Rain {
                        self.random.next_int_bound(3);
                        features::tree_oak(w, &mut self.random, tx, ty, tz);
                    } else {
                        self.random.next_int_bound(10);
                        features::tree_oak(w, &mut self.random, tx, ty, tz);
                    }
                }
                false
            }

            3 => {
                let biome = self.phase_biome();

                for _ in 0..2 {
                    let (x, y, z) = self.scatter(xo, zo, 128);
                    features::flower_feature(
                        w, &mut self.random, &mut self.pending_flowers, x, y, z, blocks::FLOWER,
                    );
                }
                if self.random.next_int_bound(2) == 0 {
                    let (x, y, z) = self.scatter(xo, zo, 128);
                    features::flower_feature(
                        w, &mut self.random, &mut self.pending_flowers, x, y, z, blocks::ROSE,
                    );
                }
                if self.random.next_int_bound(4) == 0 {
                    let (x, y, z) = self.scatter(xo, zo, 128);
                    features::mushroom_feature(
                        w, &mut self.random, &mut self.pending_mushrooms, x, y, z,
                        blocks::MUSHROOM_BROWN,
                    );
                }
                if self.random.next_int_bound(8) == 0 {
                    let (x, y, z) = self.scatter(xo, zo, 128);
                    features::mushroom_feature(
                        w, &mut self.random, &mut self.pending_mushrooms, x, y, z,
                        blocks::MUSHROOM_RED,
                    );
                }

                for _ in 0..10 {
                    let (x, y, z) = self.scatter(xo, zo, 128);
                    features::reeds_feature(w, &mut self.random, x, y, z);
                }

                let cacti = if biome == BiomeId::Desert { 5 } else { 0 };
                for _ in 0..cacti {
                    let (x, y, z) = self.scatter(xo, zo, 128);
                    features::cactus_feature(w, &mut self.random, x, y, z);
                }
                false
            }

            4 => {
                const SPRING_WATER_TRIES: i32 = 50;
                const SPRING_LAVA_TRIES: i32 = 20;
                for _ in 0..SPRING_WATER_TRIES {
                    let x = xo + self.random.next_int_bound(16) + 8;
                    let bound = self.random.next_int_bound(120) + 8;
                    let y = self.random.next_int_bound(bound);
                    let z = zo + self.random.next_int_bound(16) + 8;
                    features::spring_feature(w, x, y, z, blocks::WATER);
                }
                for _ in 0..SPRING_LAVA_TRIES {
                    let x = xo + self.random.next_int_bound(16) + 8;
                    let inner = self.random.next_int_bound(112) + 8;
                    let bound = self.random.next_int_bound(inner) + 8;
                    let y = self.random.next_int_bound(bound);
                    let z = zo + self.random.next_int_bound(16) + 8;
                    features::spring_feature(w, x, y, z, blocks::LAVA);
                }
                false
            }

            _ => {
                features::snow_cap(w, chunk_x, chunk_z, &self.m_temp);
                true
            }
        }
    }

    /// Port of worldPlaceFlowers, which the C++ keeps as a free function over a
    /// file static list.
    pub fn place_flowers<W: World>(&mut self, w: &mut W) {
        features::world_place_flowers(w, &mut self.pending_flowers);
    }

    pub fn place_mushrooms<W: World>(&mut self, w: &mut W) {
        features::world_place_mushrooms(w, &mut self.pending_mushrooms);
    }

    fn phase_biome(&self) -> BiomeId {
        BIOMES[self.m_phase_biome as usize]
    }

    /// The x, y, z triple phase 3 draws over and over, in that order.
    fn scatter(&mut self, xo: i32, zo: i32, y_bound: i32) -> (i32, i32, i32) {
        let x = xo + self.random.next_int_bound(16) + 8;
        let y = self.random.next_int_bound(y_bound);
        let z = zo + self.random.next_int_bound(16) + 8;
        (x, y, z)
    }
}

const ORE_COAL: u8 = 16;
const ORE_IRON: u8 = 15;
const ORE_GOLD: u8 = 14;
const ORE_REDSTONE: u8 = 73;
const ORE_EMERALD: u8 = 56;
const ORE_LAPIS: u8 = 21;

const BIOMES: [BiomeId; 10] = [
    BiomeId::Tundra,
    BiomeId::Savanna,
    BiomeId::Desert,
    BiomeId::Swamp,
    BiomeId::Taiga,
    BiomeId::Shrub,
    BiomeId::Forest,
    BiomeId::Plains,
    BiomeId::Seasonal,
    BiomeId::Rain,
];

/// Port of chunkGenerateTerrain. `caves` is the GEN_FEATURE_CAVES mask bit.
pub fn chunk_generate_terrain<W: World>(
    gen: &mut McpeGen,
    w: &mut W,
    cx: i32,
    cz: i32,
    caves: bool,
) {
    gen.random.set_seed(
        ((cx as u32)
            .wrapping_mul(341872712)
            .wrapping_add((cz as u32).wrapping_mul(132899541))) as i32,
    );
    gen.compute_biome(cx, cz);
    gen.prepare_chunk(w, cx, cz);
    gen.build_surfaces_chunk(w, cx, cz);

    if caves {
        let seed = gen.world_seed;
        cave_feature(w, seed, cx, cz);
    }
}
