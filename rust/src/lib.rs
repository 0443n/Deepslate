//! Rust half of Deepslate, linked into the psp-gcc EBOOT as a static archive.
//!
//! The PSP target rustc offers is o32, while every psp-gcc object and every
//! pspsdk library is EABI32. The two agree only for functions of at most four
//! integer or pointer sized arguments returning one integer or pointer, so
//! every symbol exported here stays inside that shape. Floats cross as their
//! IEEE bit pattern in a u32. See tools/patch-eflags.py for the other half of
//! the arrangement.

// Hosted builds keep std so `cargo test` can check the port against vectors
// taken from the C++ it replaces. The PSP build is no_std.
#![cfg_attr(target_os = "psp", no_std)]

extern crate alloc;

pub mod biome;
pub mod blocks;
pub mod caves;
pub mod fdlibm;
pub mod features;
#[cfg(target_os = "psp")]
pub mod ffi;
#[cfg(target_os = "psp")]
mod heap;
pub mod mcpegen;
pub mod mth;
pub mod newlib;
pub mod nether;
pub mod noise;
#[cfg(target_os = "psp")]
pub mod psp_world;
pub mod random;
pub mod world;

pub use biome::{biome_surface, classify_biome, BiomeId};
pub use noise::{ImprovedNoise, PerlinNoise};
pub use random::Random;
pub use world::World;

#[cfg(target_os = "psp")]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

/// Boot time proof that the o32 and EABI32 halves still agree on argument
/// passing. Called with 1,2,3,4 it must return 1234, so a toolchain change
/// that breaks the convention shows up at startup, not as corrupt terrain.
#[no_mangle]
pub extern "C" fn ds_abi_check(a: i32, b: i32, c: i32, d: i32) -> i32 {
    a.wrapping_mul(1000) + b.wrapping_mul(100) + c.wrapping_mul(10) + d
}

/// The float half of the same check. It goes out through newlib's sqrtf, so a
/// wrong answer means the one float shape the two conventions share stopped
/// being shared. Bits in, bits out, because the boundary itself takes integers.
#[no_mangle]
pub extern "C" fn ds_abi_check_f(bits: i32) -> i32 {
    newlib::sqrtf(f32::from_bits(bits as u32)).to_bits() as i32
}
