//! The deferred half of the flower and mushroom features.
//!
//! Both pick candidate spots while a chunk is generating but can only judge the
//! light once the whole window is lit, so the spots wait in a list. The C++
//! keeps that list in a file static std::vector capped at 4096, this is the same
//! cap as a fixed array so the port needs no allocator.

pub const PENDING_MAX: usize = 4096;

#[derive(Clone, Copy, Default)]
pub struct PendingSpot {
    pub x: i32,
    pub y: i32,
    pub z: i32,
    pub tile: u8,
}

pub struct PendingList {
    spots: [PendingSpot; PENDING_MAX],
    len: usize,
    /// Spots thrown away because the list was full, reported as a diagnostic.
    pub drops: u32,
}

impl PendingList {
    pub const fn new() -> Self {
        Self {
            spots: [PendingSpot {
                x: 0,
                y: 0,
                z: 0,
                tile: 0,
            }; PENDING_MAX],
            len: 0,
            drops: 0,
        }
    }

    /// False when the list was already full, matching the C++ `continue`.
    pub fn push(&mut self, spot: PendingSpot) -> bool {
        if self.len >= PENDING_MAX {
            self.drops += 1;
            return false;
        }
        self.spots[self.len] = spot;
        self.len += 1;
        true
    }

    pub fn as_slice(&self) -> &[PendingSpot] {
        &self.spots[..self.len]
    }

    pub fn clear(&mut self) {
        self.len = 0;
    }
}

impl Default for PendingList {
    fn default() -> Self {
        Self::new()
    }
}
