//! Port of src/world/level/levelgen/feature_tree_birch.cpp.

use super::common::tree_basic;
use crate::blocks::{LEAF_BIRCH, LOG_BIRCH};
use crate::random::Random;
use crate::world::World;

pub fn tree_birch<W: World>(w: &mut W, random: &mut Random, x: i32, y: i32, z: i32) {
    tree_basic(w, random, x, y, z, 5, LEAF_BIRCH, LOG_BIRCH);
}
