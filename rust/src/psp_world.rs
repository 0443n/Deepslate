//! The World implementation the PSP build uses, forwarding to the C++ chunk
//! store through the shims in src/rs/rs.cpp.

use crate::world::World;

/// Opaque stand in for the C++ World, which the port only ever passes back.
#[repr(C)]
pub struct CWorld {
    _private: [u8; 0],
}

extern "C" {
    fn ds_world_block(w: *mut CWorld, x: i32, y: i32, z: i32) -> i32;
    fn ds_world_ready(w: *mut CWorld, x: i32, z: i32) -> i32;
    fn ds_world_light_raw(w: *mut CWorld, x: i32, y: i32, z: i32) -> i32;
    fn ds_world_can_see_sky(w: *mut CWorld, x: i32, y: i32, z: i32) -> i32;
    fn ds_world_set(w: *mut CWorld, x: i32, y: i32, z_id_data: i32);
    fn ds_world_schedule(w: *mut CWorld, x: i32, y: i32, z_id_delay: i32);
    fn ds_world_put(w: *mut CWorld, x: i32, y: i32, z_id: i32);
    fn ds_world_column_get(w: *mut CWorld, x: i32, z: i32, out128: *mut u8);
    fn ds_world_column_put(w: *mut CWorld, x: i32, z: i32, in128: *const u8);
}

/// z occupies the low 16 bits, so it must survive the round trip as a signed
/// short. The generator never reaches further than a few chunks past the world.
fn pack(z: i32, a: u8, b: u8) -> i32 {
    debug_assert!(z >= i16::MIN as i32 && z <= i16::MAX as i32);
    (z as u16 as i32) | ((a as i32) << 16) | ((b as i32) << 24)
}

pub struct PspWorld {
    raw: *mut CWorld,
}

impl PspWorld {
    /// # Safety
    /// `raw` must be a live C++ World for as long as this value is used.
    pub unsafe fn new(raw: *mut CWorld) -> PspWorld {
        PspWorld { raw }
    }
}

impl World for PspWorld {
    fn block(&self, x: i32, y: i32, z: i32) -> u8 {
        unsafe { ds_world_block(self.raw, x, y, z) as u8 }
    }

    fn ready(&self, x: i32, z: i32) -> bool {
        unsafe { ds_world_ready(self.raw, x, z) != 0 }
    }

    fn set_block_and_data(&mut self, x: i32, y: i32, z: i32, id: u8, data: u8) {
        unsafe { ds_world_set(self.raw, x, y, pack(z, id, data)) }
    }

    fn schedule_tick(&mut self, x: i32, y: i32, z: i32, id: u8, delay: i32) {
        unsafe { ds_world_schedule(self.raw, x, y, pack(z, id, delay as u8)) }
    }

    fn light_raw(&self, x: i32, y: i32, z: i32) -> i32 {
        unsafe { ds_world_light_raw(self.raw, x, y, z) }
    }

    fn can_see_sky(&self, x: i32, y: i32, z: i32) -> bool {
        unsafe { ds_world_can_see_sky(self.raw, x, y, z) != 0 }
    }

    fn put(&mut self, x: i32, y: i32, z: i32, id: u8) {
        unsafe { ds_world_put(self.raw, x, y, pack(z, id, 0)) }
    }

    fn column_get(&self, x: i32, z: i32, out: &mut [u8; 128]) {
        unsafe { ds_world_column_get(self.raw, x, z, out.as_mut_ptr()) }
    }

    fn column_put(&mut self, x: i32, z: i32, col: &[u8; 128]) {
        unsafe { ds_world_column_put(self.raw, x, z, col.as_ptr()) }
    }
}

extern "C" {
    fn ds_world_data(w: *mut CWorld, x: i32, y: i32, z: i32) -> i32;
}

impl crate::pathfinder::PathWorld for PspWorld {
    fn block(&self, x: i32, y: i32, z: i32) -> i32 {
        unsafe { ds_world_block(self.raw, x, y, z) }
    }
    fn data(&self, x: i32, y: i32, z: i32) -> i32 {
        unsafe { ds_world_data(self.raw, x, y, z) }
    }
}

extern "C" {
    fn ds_world_brightness(w: *mut CWorld, x: i32, y: i32, z: i32) -> i32;
}

impl crate::mob::ctx::MobWorld for PspWorld {
    fn block(&self, x: i32, y: i32, z: i32) -> i32 {
        unsafe { ds_world_block(self.raw, x, y, z) }
    }
    fn brightness(&self, x: i32, y: i32, z: i32) -> f32 {
        unsafe { ds_world_brightness(self.raw, x, y, z) as f32 * 0.001 }
    }
    fn can_see_sky(&self, x: i32, y: i32, z: i32) -> bool {
        unsafe { ds_world_can_see_sky(self.raw, x, y, z) != 0 }
    }
    fn as_path(&self) -> &dyn crate::pathfinder::PathWorld {
        self
    }
}
