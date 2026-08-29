//! Port of src/world/level/levelgen/feature_tree_oak.cpp.

use super::common::tree_basic;
use crate::blocks::{LEAF_OAK, LOG_OAK};
use crate::random::Random;
use crate::world::World;

pub fn tree_oak<W: World>(w: &mut W, random: &mut Random, x: i32, y: i32, z: i32) {
    tree_basic(w, random, x, y, z, 4, LEAF_OAK, LOG_OAK);
}
