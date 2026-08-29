//! sinf and cosf as newlib implements them, which is plain fdlibm.
//!
//! The libm crate is not a substitute. Measured against newlib on the PSP over
//! 14421 samples it disagrees on roughly one in ten, always by a single ulp,
//! and levelgen feeds these results straight into Mth::floor and radius tests
//! where one ulp moves a block. Constants are written as bit patterns so no
//! decimal literal can round differently.

fn fabsf(x: f32) -> f32 {
    f32::from_bits(x.to_bits() & 0x7fff_ffff)
}

const HALF: f32 = 0.5;
const ONE: f32 = 1.0;

const S1: f32 = f32::from_bits(0xbe2a_aaab);
const S2: f32 = f32::from_bits(0x3c08_8889);
const S3: f32 = f32::from_bits(0xb950_0d01);
const S4: f32 = f32::from_bits(0x3638_ef1b);
const S5: f32 = f32::from_bits(0xb2d7_2f34);
const S6: f32 = f32::from_bits(0x2f2e_c9d3);

fn kernel_sinf(x: f32, y: f32, iy: i32) -> f32 {
    let ix = x.to_bits() & 0x7fff_ffff;
    if ix < 0x3200_0000 && x as i32 == 0 {
        return x;
    }
    let z = x * x;
    let v = z * x;
    let r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if iy == 0 {
        x + v * (S1 + z * r)
    } else {
        x - ((z * (HALF * y - v * r) - y) - v * S1)
    }
}

const C1: f32 = f32::from_bits(0x3d2a_aaab);
const C2: f32 = f32::from_bits(0xbab6_0b61);
const C3: f32 = f32::from_bits(0x37d0_0d01);
const C4: f32 = f32::from_bits(0xb493_f27c);
const C5: f32 = f32::from_bits(0x310f_74f6);
const C6: f32 = f32::from_bits(0xad47_d74e);

fn kernel_cosf(x: f32, y: f32) -> f32 {
    let ix = x.to_bits() & 0x7fff_ffff;
    if ix < 0x3200_0000 && x as i32 == 0 {
        return ONE;
    }
    let z = x * x;
    let r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if ix < 0x3e99_999a {
        ONE - (0.5 * z - (z * r - x * y))
    } else {
        // Splitting off qx keeps the subtraction above from cancelling once x
        // is large enough for 1 - z/2 to lose its leading bits.
        let qx = if ix > 0x3f48_0000 {
            0.28125f32
        } else {
            f32::from_bits(ix - 0x0100_0000)
        };
        let hz = 0.5 * z - qx;
        let a = ONE - qx;
        a - (hz - (z * r - x * y))
    }
}

const INVPIO2: f32 = f32::from_bits(0x3f22_f984);
const PIO2_1: f32 = f32::from_bits(0x3fc9_0f80);
const PIO2_1T: f32 = f32::from_bits(0x3735_4443);
const PIO2_2: f32 = f32::from_bits(0x3735_4400);
const PIO2_2T: f32 = f32::from_bits(0x2e85_a308);
const PIO2_3: f32 = f32::from_bits(0x2e85_a300);
const PIO2_3T: f32 = f32::from_bits(0x248d_3132);

/// High words of n*(pi/2) for n in 1..=32, used to spot the arguments where the
/// first reduction step cancels and a second is needed.
const NPIO2_HW: [u32; 32] = [
    0x3fc9_0f00, 0x4049_0f00, 0x4096_cb00, 0x40c9_0f00, 0x40fb_5300, 0x4116_cb00, 0x412f_ed00,
    0x4149_0f00, 0x4162_3100, 0x417b_5300, 0x418a_3a00, 0x4196_cb00, 0x41a3_5c00, 0x41af_ed00,
    0x41bc_7e00, 0x41c9_0f00, 0x41d5_a000, 0x41e2_3100, 0x41ee_c200, 0x41fb_5300, 0x4203_f200,
    0x420a_3a00, 0x4210_8300, 0x4216_cb00, 0x421d_1400, 0x4223_5c00, 0x4229_a500, 0x422f_ed00,
    0x4236_3600, 0x423c_7e00, 0x4242_c700, 0x4249_0f00,
];

/// Reduces `x` into y[0] + y[1] on (-pi/4, pi/4] and returns the quadrant.
/// Returns None above 2^7*(pi/2), where fdlibm hands off to kernel_rem_pio2f.
fn rem_pio2f(x: f32, y: &mut [f32; 2]) -> Option<i32> {
    let hx = x.to_bits() as i32;
    let ix = (hx & 0x7fff_ffff) as u32;

    if ix <= 0x3f49_0fd8 {
        y[0] = x;
        y[1] = 0.0;
        return Some(0);
    }

    if ix < 0x4016_cbe4 {
        // |x| < 3pi/4, so the quadrant is known and only the split of pi/2
        // matters. Right at pi/2 the 24+24 bit split cancels, hence the retry.
        let near_pio2 = (ix & 0xffff_fff0) == 0x3fc9_0fd0;
        if hx > 0 {
            let mut z = x - PIO2_1;
            if !near_pio2 {
                y[0] = z - PIO2_1T;
                y[1] = (z - y[0]) - PIO2_1T;
            } else {
                z -= PIO2_2;
                y[0] = z - PIO2_2T;
                y[1] = (z - y[0]) - PIO2_2T;
            }
            return Some(1);
        }
        let mut z = x + PIO2_1;
        if !near_pio2 {
            y[0] = z + PIO2_1T;
            y[1] = (z - y[0]) + PIO2_1T;
        } else {
            z += PIO2_2;
            y[0] = z + PIO2_2T;
            y[1] = (z - y[0]) + PIO2_2T;
        }
        return Some(-1);
    }

    if ix <= 0x4349_0f80 {
        let t = fabsf(x);
        let n = (t * INVPIO2 + HALF) as i32;
        let fnf = n as f32;
        let r0 = t - fnf * PIO2_1;
        let mut r = r0;
        let mut w = fnf * PIO2_1T;

        if n < 32 && (ix & 0xffff_ff00) != NPIO2_HW[(n - 1) as usize] {
            y[0] = r - w;
        } else {
            let j = (ix >> 23) as i32;
            y[0] = r - w;
            let mut i = j - ((y[0].to_bits() >> 23) & 0xff) as i32;
            if i > 8 {
                let t2 = r;
                w = fnf * PIO2_2;
                r = t2 - w;
                w = fnf * PIO2_2T - ((t2 - r) - w);
                y[0] = r - w;
                i = j - ((y[0].to_bits() >> 23) & 0xff) as i32;
                if i > 25 {
                    let t3 = r;
                    w = fnf * PIO2_3;
                    r = t3 - w;
                    w = fnf * PIO2_3T - ((t3 - r) - w);
                    y[0] = r - w;
                }
            }
        }
        y[1] = (r - y[0]) - w;

        if hx < 0 {
            y[0] = -y[0];
            y[1] = -y[1];
            return Some(-n);
        }
        return Some(n);
    }

    None
}

/// Newlib's sinf. Above 2^7*(pi/2) it falls through to the libm crate rather
/// than carrying fdlibm's kernel_rem_pio2f and its 66 word table, which no
/// argument levelgen produces ever reaches. Results there may differ from the
/// console by an ulp and are not part of the terrain contract.
pub fn sinf(x: f32) -> f32 {
    let ix = x.to_bits() & 0x7fff_ffff;
    if ix <= 0x3f49_0fd8 {
        return kernel_sinf(x, 0.0, 0);
    }
    if ix >= 0x7f80_0000 {
        return x - x;
    }
    let mut y = [0.0f32; 2];
    match rem_pio2f(x, &mut y) {
        None => libm::sinf(x),
        Some(n) => match n & 3 {
            0 => kernel_sinf(y[0], y[1], 1),
            1 => kernel_cosf(y[0], y[1]),
            2 => -kernel_sinf(y[0], y[1], 1),
            _ => -kernel_cosf(y[0], y[1]),
        },
    }
}

/// Newlib's cosf, with the same hand off above 2^7*(pi/2) as [`sinf`].
pub fn cosf(x: f32) -> f32 {
    let ix = x.to_bits() & 0x7fff_ffff;
    if ix <= 0x3f49_0fd8 {
        return kernel_cosf(x, 0.0);
    }
    if ix >= 0x7f80_0000 {
        return x - x;
    }
    let mut y = [0.0f32; 2];
    match rem_pio2f(x, &mut y) {
        None => libm::cosf(x),
        Some(n) => match n & 3 {
            0 => kernel_cosf(y[0], y[1]),
            1 => -kernel_sinf(y[0], y[1], 1),
            2 => -kernel_cosf(y[0], y[1]),
            _ => kernel_sinf(y[0], y[1], 1),
        },
    }
}
