//! The generator entry points the C++ calls, in the four argument shape both
//! ABIs agree on. See src/world/level/levelgen/mcpegen.h for the C++ side.

use crate::psp_spawn::{CLevel, PspSpawnHost};
use crate::spawner::Spawner;
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

// --- pathfinder ---------------------------------------------------------

use crate::pathfinder::{self, PathOut, PathQuery};


/// Takes the 256 byte block flag table built from Tile::tiles, so the tile
/// properties stay defined in one place.
#[no_mangle]
pub extern "C" fn ds_path_init(flags: *const u8) {
    if flags.is_null() {
        return;
    }
    let mut table = [0u8; 256];
    unsafe {
        ptr::copy_nonoverlapping(flags, table.as_mut_ptr(), 256);
    }
    pathfinder::shared_init(table);
}

#[no_mangle]
pub extern "C" fn ds_path_free() {
    pathfinder::shared_free();
}

/// Returns 1 when out holds a path. One call per repath, which is the same
/// granularity the generator crosses at.
#[no_mangle]
pub extern "C" fn ds_path_find(w: *mut CWorld, q: *const PathQuery, out: *mut PathOut) -> i32 {
    unsafe {
        if q.is_null() || out.is_null() {
            return 0;
        }
        let world = PspWorld::new(w);
        pathfinder::shared_find(&world, &*q, &mut *out) as i32
    }
}

// --- mob ai -------------------------------------------------------------

use crate::mob::{MobAi, MobIn, MobOut};

/// One AI per mob, allocated when the mob first ticks and freed when it is
/// removed. The seed makes each mob its own random stream, so a herd that
/// spawned together does not move in lockstep.
#[no_mangle]
pub extern "C" fn ds_mob_new(kind: i32, seed: i32, tempt_item: i32) -> *mut MobAi {
    Box::into_raw(Box::new(MobAi::new(kind, seed, tempt_item)))
}

#[no_mangle]
pub extern "C" fn ds_mob_free(ai: *mut MobAi) {
    if !ai.is_null() {
        unsafe { drop(Box::from_raw(ai)) };
    }
}

#[no_mangle]
pub extern "C" fn ds_mob_tick(
    ai: *mut MobAi,
    w: *mut CWorld,
    input: *const MobIn,
    out: *mut MobOut,
) {
    unsafe {
        if ai.is_null() || input.is_null() || out.is_null() {
            return;
        }
        let world = PspWorld::new(w);
        (*ai).tick(&*input, &world, &mut *out);
    }
}

/// Fills buf with the mob's running goals for the console trace, and packs the
/// path progress and target slot into the return so one call carries both.
#[no_mangle]
pub extern "C" fn ds_mob_debug(ai: *mut MobAi, buf: *mut u8, cap: i32) -> i32 {
    unsafe {
        if ai.is_null() || buf.is_null() || cap <= 0 {
            return 0;
        }
        let slice = core::slice::from_raw_parts_mut(buf, cap as usize);
        let n = (*ai).describe(slice);
        // describe fills up to cap, so the terminator has to displace the last
        // byte rather than sit past it.
        let end = if n < slice.len() { n } else { slice.len() - 1 };
        slice[end] = 0;
        let (index, len) = (*ai).nav_progress();
        ((*ai).target_slot() << 24) | ((index & 0xff) << 16) | (len & 0xffff)
    }
}

// --- mob spawning, see rust/src/spawner.rs --------------------------------

static mut SPAWNER: Option<Spawner> = None;

/// The spawner outlives any one call, so its stream keeps running the way the
/// C++ file scope Random it replaces did.
fn spawner(seed: i32) -> &'static mut Spawner {
    unsafe {
        let slot = &mut *core::ptr::addr_of_mut!(SPAWNER);
        if slot.is_none() {
            *slot = Some(Spawner::new(seed));
        }
        slot.as_mut().unwrap()
    }
}

#[no_mangle]
pub extern "C" fn ds_spawn_init(seed: i32) {
    spawner(seed).set_seed(seed);
}

#[no_mangle]
pub extern "C" fn ds_spawn_run(level: *mut CLevel, enemies: i32, friendlies: i32) {
    if level.is_null() {
        return;
    }
    let mut host = unsafe { PspSpawnHost::new(level) };
    spawner(0).tick(&mut host, enemies != 0, friendlies != 0);
}

#[no_mangle]
pub extern "C" fn ds_spawn_populate(level: *mut CLevel, seed: i32) {
    if level.is_null() {
        return;
    }
    let mut host = unsafe { PspSpawnHost::new(level) };
    let s = spawner(seed);
    s.set_seed(seed);
    s.populate_initial(&mut host);
}
