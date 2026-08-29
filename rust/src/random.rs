//! Port of the C++ `Random` in src/world/level/levelgen/Random.h.
//!
//! Mersenne Twister, not Java's LCG, despite the Java shaped API. Terrain
//! reproducibility depends on this matching draw for draw, so the quirks are
//! deliberate and must not be tidied up. See tests/ for the comparison.

const N: usize = 624;
const M: usize = 397;
const MATRIX_A: u32 = 0x9908b0df;
const UPPER_MASK: u32 = 0x80000000;
const LOWER_MASK: u32 = 0x7fffffff;
const MAG01: [u32; 2] = [0, MATRIX_A];

/// Layout matches the C++ class field for field, so the two can share state
/// once call sites start crossing the boundary.
#[repr(C)]
pub struct Random {
    seed: i32,
    mt: [u32; N],
    mti: i32,
    have_next_next_gaussian: bool,
    next_next_gaussian: f32,
}

impl Random {
    pub fn new(seed: i32) -> Random {
        let mut r = Random {
            seed: 0,
            mt: [0; N],
            mti: 0,
            have_next_next_gaussian: false,
            next_next_gaussian: 0.0,
        };
        r.set_seed(seed);
        r
    }

    pub fn set_seed(&mut self, seed: i32) {
        self.seed = seed;
        // Overwritten by init_genrand below, which leaves mti at N. The C++ does
        // the same, so the "seed me from 5489" path is unreachable after a seed.
        self.mti = (N + 1) as i32;
        self.have_next_next_gaussian = false;
        self.next_next_gaussian = 0.0;
        self.init_genrand(seed as u32);
    }

    pub fn seed(&self) -> i32 {
        self.seed
    }

    fn init_genrand(&mut self, s: u32) {
        self.mt[0] = s;
        for i in 1..N {
            let prev = self.mt[i - 1];
            self.mt[i] = 1812433253u32
                .wrapping_mul(prev ^ (prev >> 30))
                .wrapping_add(i as u32);
        }
        self.mti = N as i32;
    }

    fn genrand_int32(&mut self) -> u32 {
        if self.mti >= N as i32 {
            if self.mti == (N + 1) as i32 {
                self.init_genrand(5489);
            }
            for kk in 0..N - M {
                let y = (self.mt[kk] & UPPER_MASK) | (self.mt[kk + 1] & LOWER_MASK);
                self.mt[kk] = self.mt[kk + M] ^ (y >> 1) ^ MAG01[(y & 1) as usize];
            }
            for kk in N - M..N - 1 {
                let y = (self.mt[kk] & UPPER_MASK) | (self.mt[kk + 1] & LOWER_MASK);
                self.mt[kk] = self.mt[kk + M - N] ^ (y >> 1) ^ MAG01[(y & 1) as usize];
            }
            let y = (self.mt[N - 1] & UPPER_MASK) | (self.mt[0] & LOWER_MASK);
            self.mt[N - 1] = self.mt[M - 1] ^ (y >> 1) ^ MAG01[(y & 1) as usize];
            self.mti = 0;
        }

        let mut y = self.mt[self.mti as usize];
        self.mti += 1;

        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680;
        y ^= (y << 15) & 0xefc60000;
        y ^= y >> 18;
        y
    }

    fn genrand_real2(&mut self) -> f64 {
        self.genrand_int32() as f64 * (1.0 / 4294967296.0)
    }

    /// Tests bit 27, not the sign bit. The upstream constant is 0x8000000, one
    /// digit short of 0x80000000, and generated worlds depend on it.
    pub fn next_boolean(&mut self) -> bool {
        (self.genrand_int32() & 0x8000000) > 0
    }

    /// Rounded through f64 exactly as the C++ does, so the low bits agree.
    pub fn next_float(&mut self) -> f32 {
        self.genrand_real2() as f32
    }

    pub fn next_double(&mut self) -> f64 {
        self.genrand_real2()
    }

    pub fn next_int(&mut self) -> i32 {
        (self.genrand_int32() >> 1) as i32
    }

    /// Unsigned modulo, matching the C++ promotion of `n` to unsigned long. A
    /// negative or zero `n` is as undefined here as it is there.
    pub fn next_int_bound(&mut self, n: i32) -> i32 {
        (self.genrand_int32() % n as u32) as i32
    }

    pub fn next_long(&mut self) -> i32 {
        self.next_int()
    }

    pub fn next_long_bound(&mut self, n: i32) -> i32 {
        self.next_int_bound(n)
    }

    pub fn next_gaussian(&mut self) -> f32 {
        if self.have_next_next_gaussian {
            self.have_next_next_gaussian = false;
            return self.next_next_gaussian;
        }
        loop {
            let v1 = 2.0 * self.next_float() - 1.0;
            let v2 = 2.0 * self.next_float() - 1.0;
            let s = v1 * v1 + v2 * v2;
            if s < 1.0 && s != 0.0 {
                let multiplier = sqrtf(-2.0 * logf(s) / s);
                self.next_next_gaussian = v2 * multiplier;
                self.have_next_next_gaussian = true;
                return v1 * multiplier;
            }
        }
    }
}

#[cfg(target_os = "psp")]
fn sqrtf(x: f32) -> f32 {
    libm::sqrtf(x)
}

#[cfg(target_os = "psp")]
fn logf(x: f32) -> f32 {
    libm::logf(x)
}

#[cfg(not(target_os = "psp"))]
fn sqrtf(x: f32) -> f32 {
    x.sqrt()
}

#[cfg(not(target_os = "psp"))]
fn logf(x: f32) -> f32 {
    x.ln()
}
