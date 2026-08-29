//! Port of the src/world/level/levelgen/feature_*.cpp family, one module per
//! file. Every one of them writes through the World trait rather than owning
//! terrain, which is how the C++ half stays the single source of block storage.

pub mod cactus;
pub mod clay;
pub mod common;
pub mod flower;
pub mod lake;
pub mod mushroom;
pub mod ore;
pub mod pending;
pub mod reeds;
pub mod snow;
pub mod spring;
pub mod tree_birch;
pub mod tree_oak;
pub mod tree_pine;
pub mod tree_spruce;

pub use cactus::cactus_feature;
pub use clay::clay_feature;
pub use common::{heightmap_at, is_solid_gen, is_tree_clear, set_block, tree_basic, tree_space_clear};
pub use flower::{flower_feature, world_place_flowers};
pub use lake::lake_feature;
pub use mushroom::{mushroom_feature, world_place_mushrooms};
pub use ore::ore_feature;
pub use pending::{PendingList, PendingSpot};
pub use reeds::reeds_feature;
pub use snow::snow_cap;
pub use spring::spring_feature;
pub use tree_birch::tree_birch;
pub use tree_oak::tree_oak;
pub use tree_pine::tree_pine;
pub use tree_spruce::tree_spruce;
