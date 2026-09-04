//! The libm entry points the port needs, taken from newlib on the PSP so they
//! are the very functions the C++ half was calling.
//!
//! A single float argument and a float return live in $f12 and $f0 under both
//! o32 and EABI32, which is the one float shape the two conventions share. Two
//! float arguments already disagree, so nothing wider belongs here.
//! ds_abi_check_f proves the shape at boot.

#[cfg(target_os = "psp")]
mod imp {
    extern "C" {
        #[link_name = "sqrtf"]
        fn c_sqrtf(x: f32) -> f32;
        #[link_name = "logf"]
        fn c_logf(x: f32) -> f32;
        #[link_name = "sinf"]
        fn c_sinf(x: f32) -> f32;
        #[link_name = "cosf"]
        fn c_cosf(x: f32) -> f32;
        // Two float arguments do not agree across the boundary, so the angle
        // goes out and comes back as a bit pattern through a C++ shim.
        #[link_name = "ds_atan2f"]
        fn c_atan2f(y_bits: u32, x_bits: u32) -> u32;
    }

    pub fn sqrtf(x: f32) -> f32 {
        unsafe { c_sqrtf(x) }
    }
    pub fn logf(x: f32) -> f32 {
        unsafe { c_logf(x) }
    }
    pub fn sinf(x: f32) -> f32 {
        unsafe { c_sinf(x) }
    }
    pub fn cosf(x: f32) -> f32 {
        unsafe { c_cosf(x) }
    }
    pub fn atan2f(y: f32, x: f32) -> f32 {
        f32::from_bits(unsafe { c_atan2f(y.to_bits(), x.to_bits()) })
    }
}

#[cfg(not(target_os = "psp"))]
mod imp {
    pub fn sqrtf(x: f32) -> f32 {
        x.sqrt()
    }
    pub fn logf(x: f32) -> f32 {
        x.ln()
    }
    pub fn sinf(x: f32) -> f32 {
        x.sin()
    }
    pub fn cosf(x: f32) -> f32 {
        x.cos()
    }
    pub fn atan2f(y: f32, x: f32) -> f32 {
        y.atan2(x)
    }
}

pub use imp::{atan2f, cosf, logf, sinf, sqrtf};
