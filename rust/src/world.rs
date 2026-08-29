//! The generator's view of the world it writes into.
//!
//! The C++ World is a palettized chunk store behind std::vector, so the port
//! cannot own it, it can only reach back into it. This trait is exactly the set
//! of operations src/world/level/levelgen/ calls on it, and nothing more.

/// Everything a feature needs from the world it is decorating.
pub trait World {
    /// Air outside the vertical range, INVISIBLE_BEDROCK over an unloaded chunk.
    fn block(&self, x: i32, y: i32, z: i32) -> u8;
    fn ready(&self, x: i32, z: i32) -> bool;
    fn set_block_and_data(&mut self, x: i32, y: i32, z: i32, id: u8, data: u8);
    fn schedule_tick(&mut self, x: i32, y: i32, z: i32, id: u8, delay: i32);
    fn light_raw(&self, x: i32, y: i32, z: i32) -> i32;
    fn can_see_sky(&self, x: i32, y: i32, z: i32) -> bool;

    /// The raw put the terrain pass uses, with no tick scheduling and no data
    /// nibble, matching blockPut.
    fn put(&mut self, x: i32, y: i32, z: i32, id: u8);
    /// INVISIBLE_BEDROCK all the way down over an unloaded chunk.
    fn column_get(&self, x: i32, z: i32, out: &mut [u8; 128]);
    fn column_put(&mut self, x: i32, z: i32, col: &[u8; 128]);
}
