//! Rust half of Deepslate, linked into the psp-gcc EBOOT as a static archive.
//!
//! The PSP target rustc offers is o32, while every psp-gcc object and every
//! pspsdk library is EABI32. The two agree only for functions of at most four
//! integer or pointer sized arguments returning one integer or pointer, so
//! every symbol exported here stays inside that shape. Floats cross as their
//! IEEE bit pattern in a u32. See tools/patch-eflags.py for the other half of
//! the arrangement.

#![no_std]

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
