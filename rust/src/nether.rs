//! Port of src/world/level/levelgen/nether_gen.cpp.

use alloc::boxed::Box;
use alloc::vec;
use core::mem::MaybeUninit;
use core::ptr::{self, addr_of_mut};

use crate::blocks::{self, WORLD_D, WORLD_H, WORLD_W};
use crate::fdlibm::cosf;
use crate::noise::PerlinNoise;
use crate::random::Random;
use crate::world::World;

// Noise is sampled on a 5x17x5 grid per chunk, so a cell spans 4 blocks across
// and 8 up.
const NG_XS: usize = 5;
const NG_YS: usize = 17;
const NG_CW: i32 = 4;
const NG_CH: i32 = 8;

const NG_SQUEEZE: f32 = 3.0;
const NG_SCALE_XZ: f32 = 684.412 * NG_SQUEEZE;
const NG_SCALE_Y: f32 = 2053.236;

const NG_LAVA_LEVEL: i32 = 32;

// Vanilla only swaps exposed netherrack for gravel or soul sand in this band
// around its sea level, and leaves every other exposed face alone.
const NG_SURFACE_LO: i32 = 60;
const NG_SURFACE_HI: i32 = 65;
const NG_SPREAD: f32 = 20.0;
const NG_OPENNESS: f32 = 8.0;

/// The calibration grid, 17 cubed like the C++ N.
const CAL_N: usize = 17;

const REGION: usize = NG_XS * NG_YS * NG_XS;

pub struct NetherGen {
    rnd: Random,
    deco_rnd: Random,
    lperlin1: PerlinNoise,
    lperlin2: PerlinNoise,
    selector: PerlinNoise,
    soul_noise: PerlinNoise,
    gravel_noise: PerlinNoise,
    ar: [f32; REGION],
    br: [f32; REGION],
    sel: [f32; REGION],
    soul: [f32; 256],
    gravel: [f32; 256],
    density: [f32; REGION],
    ripple: [f32; NG_YS],
    cap: [f32; NG_YS],
    mid: [f32; NG_YS],
    gain: [f32; NG_YS],
}

impl NetherGen {
    /// Built in place for the same reason McpeGen is, five sets of octaves do
    /// not fit on the terrain thread's stack.
    pub fn new(seed: i32) -> Box<NetherGen> {
        let mut b: Box<MaybeUninit<NetherGen>> = Box::new_uninit();
        unsafe {
            let p = b.as_mut_ptr();
            ptr::write_bytes(p, 0, 1);
            addr_of_mut!((*p).rnd).write(Random::new(seed));
            addr_of_mut!((*p).deco_rnd).write(Random::new(seed));

            // Declaration order, which is the order the shared Random is drawn.
            let r = &mut *addr_of_mut!((*p).rnd);
            addr_of_mut!((*p).lperlin1).write(PerlinNoise::new(r, 16));
            addr_of_mut!((*p).lperlin2).write(PerlinNoise::new(r, 16));
            addr_of_mut!((*p).selector).write(PerlinNoise::new(r, 8));
            addr_of_mut!((*p).soul_noise).write(PerlinNoise::new(r, 4));
            addr_of_mut!((*p).gravel_noise).write(PerlinNoise::new(r, 4));

            let mut g = b.assume_init();
            for y in 0..NG_YS {
                g.ripple[y] = cosf(y as f32 * 3.14159265 * 6.0 / NG_YS as f32) * 2.0;
                g.cap[y] = 0.0;
                g.mid[y] = 0.0;
                g.gain[y] = 1.0;
                let mut d = if y > NG_YS / 2 {
                    (NG_YS - 1 - y) as f32
                } else {
                    y as f32
                };
                if d < 4.0 {
                    d = 4.0 - d;
                    g.cap[y] = -d * d * d * 10.0;
                }
            }
            g
        }
    }

    /// The largest octave is wider than this world, so a fixed 256 block square
    /// comes out solid rock. Each height level is recentred and rescaled to a
    /// common spread, which is what keeps the caverns connected.
    pub fn calibrate(&mut self) {
        let n = CAL_N;
        let xs = (WORLD_W / NG_CW) as f32 / (n - 1) as f32;
        let ys = (NG_YS - 1) as f32 / (n - 1) as f32;
        let (s, sy) = (NG_SCALE_XZ, NG_SCALE_Y);
        let ni = n as i32;

        let mut cs = vec![0.0f32; n * n * n];
        let mut ca = vec![0.0f32; n * n * n];
        let mut cb = vec![0.0f32; n * n * n];
        self.selector.get_region(
            &mut cs, 0.0, 0.0, 0.0, ni, ni, ni,
            s / 80.0 * xs, sy / 60.0 * ys, s / 80.0 * xs,
        );
        self.lperlin1
            .get_region(&mut ca, 0.0, 0.0, 0.0, ni, ni, ni, s * xs, sy * ys, s * xs);
        self.lperlin2
            .get_region(&mut cb, 0.0, 0.0, 0.0, ni, ni, ni, s * xs, sy * ys, s * xs);

        let mut level = [0.0f32; CAL_N * CAL_N];
        for iy in 0..NG_YS {
            for ix in 0..n {
                for iz in 0..n {
                    let i = (ix * n + iz) * n + iy;
                    let lo = ca[i] / 512.0;
                    let hi = cb[i] / 512.0;
                    let t = (cs[i] / 10.0 + 1.0) / 2.0;
                    level[ix * n + iz] = if t < 0.0 {
                        lo
                    } else if t > 1.0 {
                        hi
                    } else {
                        lo + (hi - lo) * t
                    };
                }
            }
            level.sort_unstable_by(|a, b| a.partial_cmp(b).unwrap());
            // The 16th to 84th span is two standard deviations, without the
            // tails' say over it that the plain deviation would give them.
            let nf = n as f32;
            let sd = (level[(0.84 * nf * nf) as usize] - level[(0.16 * nf * nf) as usize]) * 0.5;
            self.mid[iy] = level[n * n / 2];
            self.gain[iy] = if sd > 1e-4 { NG_SPREAD / sd } else { 1.0 };
        }
    }

    fn compute_density(&mut self, cx: i32, cz: i32) {
        let (s, sy) = (NG_SCALE_XZ, NG_SCALE_Y);
        let x = (cx * (NG_XS as i32 - 1)) as f32;
        let z = (cz * (NG_XS as i32 - 1)) as f32;
        let (xs, ys) = (NG_XS as i32, NG_YS as i32);

        self.selector.get_region(
            &mut self.sel, x, 0.0, z, xs, ys, xs,
            s / 80.0, sy / 60.0, s / 80.0,
        );
        self.lperlin1
            .get_region(&mut self.ar, x, 0.0, z, xs, ys, xs, s, sy, s);
        self.lperlin2
            .get_region(&mut self.br, x, 0.0, z, xs, ys, xs, s, sy, s);

        let mut p = 0;
        for _ix in 0..NG_XS {
            for _iz in 0..NG_XS {
                for iy in 0..NG_YS {
                    let lo = self.ar[p] / 512.0;
                    let hi = self.br[p] / 512.0;
                    let t = (self.sel[p] / 10.0 + 1.0) / 2.0;
                    let mut v = if t < 0.0 {
                        lo
                    } else if t > 1.0 {
                        hi
                    } else {
                        lo + (hi - lo) * t
                    };
                    v = (v - self.mid[iy]) * self.gain[iy] - NG_OPENNESS;
                    v -= self.ripple[iy];
                    v -= self.cap[iy];
                    if iy > NG_YS - 4 {
                        let sl = (iy - (NG_YS - 4)) as f32 / 3.0;
                        v = v * (1.0 - sl) + -10.0 * sl;
                    }
                    self.density[p] = v;
                    p += 1;
                }
            }
        }
    }

    fn fill_columns<W: World>(&mut self, w: &mut W, cx: i32, cz: i32) {
        for xc in 0..NG_XS - 1 {
            for zc in 0..NG_XS - 1 {
                for yc in 0..NG_YS - 1 {
                    let y_step = 1.0 / NG_CH as f32;
                    let i00 = ((xc) * NG_XS + (zc)) * NG_YS + yc;
                    let i01 = ((xc) * NG_XS + (zc + 1)) * NG_YS + yc;
                    let i10 = ((xc + 1) * NG_XS + (zc)) * NG_YS + yc;
                    let i11 = ((xc + 1) * NG_XS + (zc + 1)) * NG_YS + yc;

                    let mut s0 = self.density[i00];
                    let mut s1 = self.density[i01];
                    let mut s2 = self.density[i10];
                    let mut s3 = self.density[i11];
                    let s0a = (self.density[i00 + 1] - s0) * y_step;
                    let s1a = (self.density[i01 + 1] - s1) * y_step;
                    let s2a = (self.density[i10 + 1] - s2) * y_step;
                    let s3a = (self.density[i11 + 1] - s3) * y_step;

                    for y in 0..NG_CH {
                        let x_step = 1.0 / NG_CW as f32;
                        let mut a0 = s0;
                        let mut a1 = s1;
                        let a0a = (s2 - s0) * x_step;
                        let a1a = (s3 - s1) * x_step;

                        for x in 0..NG_CW {
                            let z_step = 1.0 / NG_CW as f32;
                            let mut val = a0;
                            let vala = (a1 - a0) * z_step;

                            for z in 0..NG_CW {
                                let gx = cx * 16 + xc as i32 * NG_CW + x;
                                let gz = cz * 16 + zc as i32 * NG_CW + z;
                                let gy = yc as i32 * NG_CH + y;

                                let mut id = blocks::AIR;
                                if gy < NG_LAVA_LEVEL {
                                    id = blocks::CALM_LAVA;
                                }
                                if val > 0.0 {
                                    id = blocks::NETHERRACK;
                                }
                                w.put(gx, gy, gz, id);

                                val += vala;
                            }
                            a0 += a0a;
                            a1 += a1a;
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

    fn build_surfaces<W: World>(&mut self, w: &mut W, cx: i32, cz: i32) {
        let s = 1.0 / 32.0;
        self.rnd.set_seed(
            (cx as u32)
                .wrapping_mul(341872712)
                .wrapping_add((cz as u32).wrapping_mul(132899541)) as i32,
        );
        self.soul_noise.get_region(
            &mut self.soul, (cx * 16) as f32, (cz * 16) as f32, 0.0, 16, 16, 1, s, s, 1.0,
        );
        self.gravel_noise.get_region(
            &mut self.gravel, (cx * 16) as f32, 109.0134, (cz * 16) as f32, 16, 1, 16, s, 1.0, s,
        );

        for x in 0..16 {
            for z in 0..16 {
                let want_soul =
                    (self.soul[(z + x * 16) as usize] + self.rnd.next_float() * 0.2) > 0.0;
                let want_gravel =
                    (self.gravel[(z + x * 16) as usize] + self.rnd.next_float() * 0.2) > 0.0;
                let gx = cx * 16 + x;
                let gz = cz * 16 + z;

                // Counts down through the top of each exposed run, which is
                // where vanilla swaps netherrack for the patch material. The
                // pair is only reassigned inside the band, so a choice made
                // there carries on down the column.
                let mut run = -1;
                let mut top = blocks::NETHERRACK;
                let mut filler = blocks::NETHERRACK;

                for y in (0..WORLD_H).rev() {
                    if y >= WORLD_H - 1 - self.deco_rnd.next_int_bound(5)
                        || y <= self.deco_rnd.next_int_bound(5)
                    {
                        w.put(gx, y, gz, blocks::BEDROCK);
                        continue;
                    }
                    let id = w.block(gx, y, gz);
                    if id == blocks::AIR || id == blocks::CALM_LAVA {
                        run = -1;
                        continue;
                    }
                    if id != blocks::NETHERRACK {
                        continue;
                    }

                    if run == -1 {
                        if (NG_SURFACE_LO..=NG_SURFACE_HI).contains(&y) {
                            top = blocks::NETHERRACK;
                            filler = blocks::NETHERRACK;
                            if want_gravel {
                                top = blocks::GRAVEL;
                                filler = blocks::NETHERRACK;
                            }
                            if want_soul {
                                top = blocks::SOUL_SAND;
                                filler = blocks::SOUL_SAND;
                            }
                        }
                        if top == blocks::NETHERRACK {
                            run = 0;
                            continue;
                        }
                        w.put(gx, y, gz, top);
                        // Netherrack filler would only write back the block
                        // already there.
                        run = if filler == blocks::NETHERRACK { 0 } else { 3 };
                        continue;
                    }
                    if run > 0 {
                        w.put(gx, y, gz, filler);
                        run -= 1;
                    }
                }
            }
        }
    }

    fn decorate<W: World>(&mut self, w: &mut W, cx: i32, cz: i32) {
        self.deco_rnd.set_seed(
            (cx as u32)
                .wrapping_mul(93781121)
                .wrapping_add((cz as u32).wrapping_mul(65432197)) as i32,
        );

        // Glowstone hangs from whatever ceiling the blob finds. The seed block
        // is what the growth rule counts from.
        for _ in 0..10 {
            let bx = cx * 16 + self.deco_rnd.next_int_bound(16);
            let bz = cz * 16 + self.deco_rnd.next_int_bound(16);
            let by = 4 + self.deco_rnd.next_int_bound(WORLD_H - 12);
            if w.block(bx, by, bz) != blocks::AIR {
                continue;
            }
            if w.block(bx, by + 1, bz) != blocks::NETHERRACK {
                continue;
            }
            w.put(bx, by, bz, blocks::GLOWSTONE);

            for _ in 0..1500 {
                let px = bx + self.deco_rnd.next_int_bound(8) - self.deco_rnd.next_int_bound(8);
                let py = by - self.deco_rnd.next_int_bound(12);
                let pz = bz + self.deco_rnd.next_int_bound(8) - self.deco_rnd.next_int_bound(8);
                if px < 0 || pz < 0 || px >= WORLD_W || pz >= WORLD_D || py < 1 {
                    continue;
                }
                if w.block(px, py, pz) != blocks::AIR {
                    continue;
                }

                let mut touching = 0;
                let neighbours = [
                    (px - 1, py, pz),
                    (px + 1, py, pz),
                    (px, py, pz - 1),
                    (px, py, pz + 1),
                    (px, py - 1, pz),
                    (px, py + 1, pz),
                ];
                for &(nx, ny, nz) in neighbours.iter() {
                    if w.block(nx, ny, nz) == blocks::GLOWSTONE {
                        touching += 1;
                    }
                }
                if touching != 1 {
                    continue;
                }
                w.put(px, py, pz, blocks::GLOWSTONE);
            }
        }

        // Open fires on the netherrack shores. Each seed scatters, since a lone
        // point sample almost never lands on a floor.
        for _ in 0..8 {
            let bx = cx * 16 + self.deco_rnd.next_int_bound(16);
            let bz = cz * 16 + self.deco_rnd.next_int_bound(16);
            let by = 4 + self.deco_rnd.next_int_bound(WORLD_H - 12);

            for _ in 0..64 {
                let px = bx + self.deco_rnd.next_int_bound(8) - self.deco_rnd.next_int_bound(8);
                let py = by + self.deco_rnd.next_int_bound(4) - self.deco_rnd.next_int_bound(4);
                let pz = bz + self.deco_rnd.next_int_bound(8) - self.deco_rnd.next_int_bound(8);
                if px < 0 || pz < 0 || px >= WORLD_W || pz >= WORLD_D || py < 1 {
                    continue;
                }
                if w.block(px, py, pz) != blocks::AIR {
                    continue;
                }
                if w.block(px, py - 1, pz) != blocks::NETHERRACK {
                    continue;
                }
                w.put(px, py, pz, blocks::FIRE);
            }
        }
    }

    /// Port of netherGenerateChunk.
    pub fn generate_chunk<W: World>(&mut self, w: &mut W, cx: i32, cz: i32) {
        self.compute_density(cx, cz);
        self.fill_columns(w, cx, cz);
        self.build_surfaces(w, cx, cz);
        self.decorate(w, cx, cz);
    }
}
