//! Port of ImprovedNoise.cpp and PerlinNoise.cpp.
//!
//! Both are sampled straight into terrain, so every operation keeps the order
//! and the float width the C++ used. The dead members went nowhere, Synth had
//! no callers outside the virtual getValue these two override, and hashCode,
//! create, getDataSize and ImprovedNoise's never assigned `scale` field had
//! none at all.

extern crate alloc;
use alloc::vec::Vec;

use crate::random::Random;

fn lerp(t: f32, a: f32, b: f32) -> f32 {
    a + t * (b - a)
}

/// C truncates toward zero and the C++ steps back a whole one when it
/// overshot, which is a floor written the long way.
fn floor_i32(x: f32) -> i32 {
    let f = x as i32;
    if x < f as f32 {
        f - 1
    } else {
        f
    }
}

fn fade(t: f32) -> f32 {
    t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
}

pub struct ImprovedNoise {
    pub xo: f32,
    pub yo: f32,
    pub zo: f32,
    p: [i32; 512],
}

impl ImprovedNoise {
    pub fn new(random: &mut Random) -> ImprovedNoise {
        let mut n = ImprovedNoise {
            xo: random.next_float() * 256.0,
            yo: random.next_float() * 256.0,
            zo: random.next_float() * 256.0,
            p: [0; 512],
        };
        for i in 0..256 {
            n.p[i] = i as i32;
        }
        for i in 0..256 {
            let j = random.next_int_bound(256 - i as i32) as usize + i;
            n.p.swap(i, j);
            n.p[i + 256] = n.p[i];
        }
        n
    }

    fn grad(hash: i32, x: f32, y: f32, z: f32) -> f32 {
        let h = hash & 15;
        let u = if h < 8 { x } else { y };
        let v = if h < 4 {
            y
        } else if h == 12 || h == 14 {
            x
        } else {
            z
        };
        (if h & 1 == 0 { u } else { -u }) + (if h & 2 == 0 { v } else { -v })
    }

    fn grad2(hash: i32, x: f32, z: f32) -> f32 {
        let h = hash & 15;
        let u = (1 - ((h & 8) >> 3)) as f32 * x;
        let v = if h < 4 {
            0.0
        } else if h == 12 || h == 14 {
            x
        } else {
            z
        };
        (if h & 1 == 0 { u } else { -u }) + (if h & 2 == 0 { v } else { -v })
    }

    pub fn noise(&self, x_in: f32, y_in: f32, z_in: f32) -> f32 {
        let (mut x, mut y, mut z) = (x_in + self.xo, y_in + self.yo, z_in + self.zo);

        let xf = floor_i32(x);
        let yf = floor_i32(y);
        let zf = floor_i32(z);

        let (xi, yi, zi) = ((xf & 255) as usize, yf & 255, (zf & 255) as i32);

        x -= xf as f32;
        y -= yf as f32;
        z -= zf as f32;

        let (u, v, w) = (fade(x), fade(y), fade(z));

        let p = &self.p;
        let a = (p[xi] + yi) as usize;
        let aa = (p[a] + zi) as usize;
        let ab = (p[a + 1] + zi) as usize;
        let b = (p[xi + 1] + yi) as usize;
        let ba = (p[b] + zi) as usize;
        let bb = (p[b + 1] + zi) as usize;

        lerp(
            w,
            lerp(
                v,
                lerp(u, Self::grad(p[aa], x, y, z), Self::grad(p[ba], x - 1.0, y, z)),
                lerp(
                    u,
                    Self::grad(p[ab], x, y - 1.0, z),
                    Self::grad(p[bb], x - 1.0, y - 1.0, z),
                ),
            ),
            lerp(
                v,
                lerp(
                    u,
                    Self::grad(p[aa + 1], x, y, z - 1.0),
                    Self::grad(p[ba + 1], x - 1.0, y, z - 1.0),
                ),
                lerp(
                    u,
                    Self::grad(p[ab + 1], x, y - 1.0, z - 1.0),
                    Self::grad(p[bb + 1], x - 1.0, y - 1.0, z - 1.0),
                ),
            ),
        )
    }

    pub fn get_value2(&self, x: f32, y: f32) -> f32 {
        self.noise(x, y, 0.0)
    }

    pub fn get_value3(&self, x: f32, y: f32, z: f32) -> f32 {
        self.noise(x, y, z)
    }

    /// Adds one octave into `buffer`. The flat case is a separate loop in the
    /// C++ and reaches for grad2 on one of its four corners where the general
    /// case uses grad, so the two disagree by design and are kept apart here.
    #[allow(clippy::too_many_arguments)]
    pub fn add(
        &self,
        buffer: &mut [f32],
        x_in: f32,
        y_in: f32,
        z_in: f32,
        x_size: i32,
        y_size: i32,
        z_size: i32,
        xs: f32,
        ys: f32,
        zs: f32,
        pow: f32,
    ) {
        let p = &self.p;
        let scale = 1.0 / pow;
        let mut pp = 0usize;

        if y_size == 1 {
            for xx in 0..x_size {
                let mut x = (x_in + xx as f32) * xs + self.xo;
                let xf = floor_i32(x);
                let xi = (xf & 255) as usize;
                x -= xf as f32;
                let u = fade(x);

                for zz in 0..z_size {
                    let mut z = (z_in + zz as f32) * zs + self.zo;
                    let zf = floor_i32(z);
                    let zi = zf & 255;
                    z -= zf as f32;
                    let w = fade(z);

                    let a = p[xi] as usize;
                    let aa = (p[a] + zi) as usize;
                    let b = p[xi + 1] as usize;
                    let ba = (p[b] + zi) as usize;
                    let vv0 = lerp(
                        u,
                        Self::grad2(p[aa], x, z),
                        Self::grad(p[ba], x - 1.0, 0.0, z),
                    );
                    let vv2 = lerp(
                        u,
                        Self::grad(p[aa + 1], x, 0.0, z - 1.0),
                        Self::grad(p[ba + 1], x - 1.0, 0.0, z - 1.0),
                    );

                    buffer[pp] += lerp(w, vv0, vv2) * scale;
                    pp += 1;
                }
            }
            return;
        }

        // yOld spans the whole nest in the C++, not just the innermost loop, so
        // the corner cache carries over between columns.
        let mut y_old = -1;
        let (mut vv0, mut vv1, mut vv2, mut vv3) = (0.0f32, 0.0f32, 0.0f32, 0.0f32);

        for xx in 0..x_size {
            let mut x = (x_in + xx as f32) * xs + self.xo;
            let xf = floor_i32(x);
            let xi = (xf & 255) as usize;
            x -= xf as f32;
            let u = fade(x);

            for zz in 0..z_size {
                let mut z = (z_in + zz as f32) * zs + self.zo;
                let zf = floor_i32(z);
                let zi = zf & 255;
                z -= zf as f32;
                let w = fade(z);

                for yy in 0..y_size {
                    let mut y = (y_in + yy as f32) * ys + self.yo;
                    let yf = floor_i32(y);
                    let yi = yf & 255;
                    y -= yf as f32;
                    let v = fade(y);

                    if yy == 0 || yi != y_old {
                        y_old = yi;
                        let a = (p[xi] + yi) as usize;
                        let aa = (p[a] + zi) as usize;
                        let ab = (p[a + 1] + zi) as usize;
                        let b = (p[xi + 1] + yi) as usize;
                        let ba = (p[b] + zi) as usize;
                        let bb = (p[b + 1] + zi) as usize;
                        vv0 = lerp(u, Self::grad(p[aa], x, y, z), Self::grad(p[ba], x - 1.0, y, z));
                        vv1 = lerp(
                            u,
                            Self::grad(p[ab], x, y - 1.0, z),
                            Self::grad(p[bb], x - 1.0, y - 1.0, z),
                        );
                        vv2 = lerp(
                            u,
                            Self::grad(p[aa + 1], x, y, z - 1.0),
                            Self::grad(p[ba + 1], x - 1.0, y, z - 1.0),
                        );
                        vv3 = lerp(
                            u,
                            Self::grad(p[ab + 1], x, y - 1.0, z - 1.0),
                            Self::grad(p[bb + 1], x - 1.0, y - 1.0, z - 1.0),
                        );
                    }

                    let v0 = lerp(v, vv0, vv1);
                    let v1 = lerp(v, vv2, vv3);
                    buffer[pp] += lerp(w, v0, v1) * scale;
                    pp += 1;
                }
            }
        }
    }
}

/// `LEVELS` is a constant at every construction site in the generator, so the
/// octaves sit inline instead of behind the C++ array of heap pointers.
/// The octaves live behind a Vec because the C++ heap allocates them too, and
/// because McpeGen holds eleven of these and would not otherwise fit on the
/// terrain thread's stack while it is being built.
pub struct PerlinNoise {
    levels: Vec<ImprovedNoise>,
}

impl PerlinNoise {
    pub fn new(random: &mut Random, levels: usize) -> PerlinNoise {
        let mut v = Vec::with_capacity(levels);
        for _ in 0..levels {
            v.push(ImprovedNoise::new(random));
        }
        PerlinNoise { levels: v }
    }

    pub fn get_value2(&self, x: f32, y: f32) -> f32 {
        let mut value = 0.0;
        let mut pow = 1.0f32;
        for level in self.levels.iter() {
            value += level.get_value2(x * pow, y * pow) / pow;
            pow /= 2.0;
        }
        value
    }

    pub fn get_value3(&self, x: f32, y: f32, z: f32) -> f32 {
        let mut value = 0.0;
        let mut pow = 1.0f32;
        for level in self.levels.iter() {
            value += level.get_value3(x * pow, y * pow, z * pow) / pow;
            pow /= 2.0;
        }
        value
    }

    #[allow(clippy::too_many_arguments)]
    pub fn get_region(
        &self,
        buffer: &mut [f32],
        x: f32,
        y: f32,
        z: f32,
        x_size: i32,
        y_size: i32,
        z_size: i32,
        x_scale: f32,
        y_scale: f32,
        z_scale: f32,
    ) {
        let size = (x_size * y_size * z_size) as usize;
        assert!(buffer.len() >= size, "region buffer too small");
        for v in buffer[..size].iter_mut() {
            *v = 0.0;
        }

        let mut pow = 1.0f32;
        for level in self.levels.iter() {
            level.add(
                buffer,
                x,
                y,
                z,
                x_size,
                y_size,
                z_size,
                x_scale * pow,
                y_scale * pow,
                z_scale * pow,
                pow,
            );
            pow /= 2.0;
        }
    }

    /// The flat overload. Its `pow` argument is dead in the C++ too, which is
    /// where most of the apparent `pow(` calls in levelgen come from.
    pub fn get_region_flat(
        &self,
        buffer: &mut [f32],
        x: i32,
        z: i32,
        x_size: i32,
        z_size: i32,
        x_scale: f32,
        z_scale: f32,
    ) {
        self.get_region(
            buffer, x as f32, 10.0, z as f32, x_size, 1, z_size, x_scale, 1.0, z_scale,
        );
    }
}
