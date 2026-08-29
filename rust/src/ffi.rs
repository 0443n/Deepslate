//! The generator entry points the C++ calls, in the four argument shape both
//! ABIs agree on. See src/world/level/levelgen/mcpegen.h for the C++ side.

use alloc::boxed::Box;
use core::ptr;

use crate::mcpegen::{self, McpeGen};
use crate::nether::NetherGen;
use crate::psp_world::{CWorld, PspWorld};

// One terrain thread owns these for a whole world, exactly as the C++ file
// statics they replace did.
static mut GEN: *mut McpeGen = ptr::null_mut();
static mut GEN_SEED: i32 = 0;
static mut GEN_CAVES: bool = false;
static mut NETHER: *mut NetherGen = ptr::null_mut();

/// Bit 0 of the mask, which is GEN_FEATURE_CAVES.
const CAVES_BIT: i32 = 1;

#[no_mangle]
pub extern "C" fn ds_gen_init(seed: i32, gen_mask: i32) {
    unsafe {
        // Reinitialising the same seed would rewind the shared Random, so the
        // C++ keeps the generator it has and only takes the new mask.
        if !GEN.is_null() && GEN_SEED == seed {
            GEN_CAVES = gen_mask & CAVES_BIT != 0;
            return;
        }
    }
    ds_gen_free();
    unsafe {
        GEN = Box::into_raw(McpeGen::new(seed));
        GEN_SEED = seed;
        GEN_CAVES = gen_mask & CAVES_BIT != 0;
    }
}

#[no_mangle]
pub extern "C" fn ds_gen_free() {
    unsafe {
        if !GEN.is_null() {
            drop(Box::from_raw(GEN));
            GEN = ptr::null_mut();
        }
    }
}

#[no_mangle]
pub extern "C" fn ds_gen_terrain(w: *mut CWorld, cx: i32, cz: i32) {
    unsafe {
        if GEN.is_null() {
            return;
        }
        let mut world = PspWorld::new(w);
        mcpegen::chunk_generate_terrain(&mut *GEN, &mut world, cx, cz, GEN_CAVES);
    }
}

#[no_mangle]
pub extern "C" fn ds_gen_phase(w: *mut CWorld, cx: i32, cz: i32, phase: i32) -> i32 {
    unsafe {
        if GEN.is_null() {
            return 1;
        }
        let mut world = PspWorld::new(w);
        (*GEN).post_process_phase(&mut world, cx, cz, phase) as i32
    }
}

#[no_mangle]
pub extern "C" fn ds_gen_place_flowers(w: *mut CWorld) {
    unsafe {
        if GEN.is_null() {
            return;
        }
        let mut world = PspWorld::new(w);
        (*GEN).place_flowers(&mut world);
    }
}

#[no_mangle]
pub extern "C" fn ds_gen_place_mushrooms(w: *mut CWorld) {
    unsafe {
        if GEN.is_null() {
            return;
        }
        let mut world = PspWorld::new(w);
        (*GEN).place_mushrooms(&mut world);
    }
}

#[no_mangle]
pub extern "C" fn ds_nether_init(seed: i32) {
    ds_nether_free();
    unsafe {
        let mut g = NetherGen::new(seed);
        g.calibrate();
        NETHER = Box::into_raw(g);
    }
}

#[no_mangle]
pub extern "C" fn ds_nether_free() {
    unsafe {
        if !NETHER.is_null() {
            drop(Box::from_raw(NETHER));
            NETHER = ptr::null_mut();
        }
    }
}

#[no_mangle]
pub extern "C" fn ds_nether_chunk(w: *mut CWorld, cx: i32, cz: i32) {
    unsafe {
        if NETHER.is_null() {
            ds_nether_init(0);
        }
        let mut world = PspWorld::new(w);
        (*NETHER).generate_chunk(&mut world, cx, cz);
    }
}
