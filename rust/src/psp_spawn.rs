//! The SpawnHost the PSP build uses, forwarding to Level through the shims in
//! src/rs/rs.cpp.
//!
//! The shims are chatty by the standard rust/src/psp_world.rs sets, which is
//! fine here. Spawning runs every other tick and gives up after eight attempts,
//! not once per mob per tick.

use crate::spawner::SpawnHost;

/// Opaque stand in for the C++ Level, which the port only ever passes back.
#[repr(C)]
pub struct CLevel {
    _private: [u8; 0],
}

/// The offer that crosses to C++, which builds the mob and answers whether it
/// accepted the spot. Laid out by hand on both sides, see DsSpawnReq in rs.h.
#[repr(C)]
pub struct SpawnReq {
    pub mob_id: i32,
    pub baby: i32,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub y_rot: f32,
}

extern "C" {
    fn ds_spawn_tile(l: *mut CLevel, x: i32, y: i32, z: i32) -> i32;
    fn ds_spawn_solid(l: *mut CLevel, x: i32, y: i32, z: i32) -> i32;
    fn ds_spawn_brightness(l: *mut CLevel, x: i32, y: i32, z: i32) -> i32;
    fn ds_spawn_top_solid(l: *mut CLevel, x: i32, z: i32) -> i32;
    fn ds_spawn_chunk_ready(l: *mut CLevel, cx: i32, cz: i32) -> i32;
    fn ds_spawn_count_base(l: *mut CLevel, base: i32) -> i32;
    fn ds_spawn_count_type(l: *mut CLevel, mob_id: i32) -> i32;
    fn ds_spawn_count_creatures_near(l: *mut CLevel, px_bits: i32, pz_bits: i32, r: i32) -> i32;
    /// Fills three floats, and answers 0 when there is no player.
    fn ds_spawn_player(l: *mut CLevel, out3: *mut f32) -> i32;
    fn ds_spawn_player_within(l: *mut CLevel, pos3: *const f32, r: i32) -> i32;
    fn ds_spawn_peaceful(l: *mut CLevel) -> i32;
    fn ds_spawn_slot_count(l: *mut CLevel) -> i32;
    /// Packs cx in the low half and cz in the high half, or answers -1.
    fn ds_spawn_slot_chunk(l: *mut CLevel, i: i32) -> i32;
    fn ds_spawn_place(l: *mut CLevel, req: *const SpawnReq) -> i32;
}

pub struct PspSpawnHost {
    raw: *mut CLevel,
}

impl PspSpawnHost {
    /// # Safety
    /// `raw` must be a live C++ Level for as long as this value is used.
    pub unsafe fn new(raw: *mut CLevel) -> PspSpawnHost {
        PspSpawnHost { raw }
    }
}

impl SpawnHost for PspSpawnHost {
    fn tile(&self, x: i32, y: i32, z: i32) -> i32 {
        unsafe { ds_spawn_tile(self.raw, x, y, z) }
    }
    fn solid_blocking(&self, x: i32, y: i32, z: i32) -> bool {
        unsafe { ds_spawn_solid(self.raw, x, y, z) != 0 }
    }
    fn brightness(&self, x: i32, y: i32, z: i32) -> i32 {
        unsafe { ds_spawn_brightness(self.raw, x, y, z) }
    }
    fn top_solid(&self, x: i32, z: i32) -> i32 {
        unsafe { ds_spawn_top_solid(self.raw, x, z) }
    }
    fn chunk_ready(&self, cx: i32, cz: i32) -> bool {
        unsafe { ds_spawn_chunk_ready(self.raw, cx, cz) != 0 }
    }
    fn count_base(&self, base: i32) -> i32 {
        unsafe { ds_spawn_count_base(self.raw, base) }
    }
    fn count_type(&self, mob_id: i32) -> i32 {
        unsafe { ds_spawn_count_type(self.raw, mob_id) }
    }
    fn count_creatures_near(&self, px: f32, pz: f32, r: f32) -> i32 {
        // Two floats do not survive the o32 and EABI32 bridge, so they cross as
        // bit patterns. The radius is whole blocks and rides as an int.
        unsafe {
            ds_spawn_count_creatures_near(
                self.raw,
                px.to_bits() as i32,
                pz.to_bits() as i32,
                r as i32,
            )
        }
    }
    fn player(&self) -> Option<(f32, f32, f32)> {
        let mut p = [0.0f32; 3];
        let ok = unsafe { ds_spawn_player(self.raw, p.as_mut_ptr()) };
        if ok == 0 {
            return None;
        }
        Some((p[0], p[1], p[2]))
    }
    fn player_within(&self, x: f32, y: f32, z: f32, r: f32) -> bool {
        let p = [x, y, z];
        unsafe { ds_spawn_player_within(self.raw, p.as_ptr(), r as i32) != 0 }
    }
    fn peaceful(&self) -> bool {
        unsafe { ds_spawn_peaceful(self.raw) != 0 }
    }
    fn slot_count(&self) -> i32 {
        unsafe { ds_spawn_slot_count(self.raw) }
    }
    fn slot_chunk(&self, i: i32) -> Option<(i32, i32)> {
        let packed = unsafe { ds_spawn_slot_chunk(self.raw, i) };
        if packed < 0 {
            return None;
        }
        Some((packed as i16 as i32, (packed >> 16) as i16 as i32))
    }
    fn place(&mut self, mob_id: i32, x: f32, y: f32, z: f32, y_rot: f32, baby: bool) -> bool {
        let req = SpawnReq { mob_id, baby: baby as i32, x, y, z, y_rot };
        unsafe { ds_spawn_place(self.raw, &req) != 0 }
    }
}
