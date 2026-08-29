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
}

pub use imp::{cosf, logf, sinf, sqrtf};
