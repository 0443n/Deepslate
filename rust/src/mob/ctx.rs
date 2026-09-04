// What a goal is handed each tick. Everything the AI can read about the world
// arrives either in the snapshot or through MobWorld, and everything it wants
// done leaves in MobOut.

use super::look::LookControl;
use super::nav::Navigation;
use super::{is_animal, EntityView, MobIn, MobOut, TARGET_ATTACKER, TARGET_NONE, TARGET_PLAYER};
use crate::blocks;
use crate::newlib::sqrtf;
use crate::pathfinder::PathWorld;
use crate::random::Random;

pub trait MobWorld {
    fn block(&self, x: i32, y: i32, z: i32) -> i32;
    fn brightness(&self, x: i32, y: i32, z: i32) -> f32;
    fn can_see_sky(&self, x: i32, y: i32, z: i32) -> bool;
    fn as_path(&self) -> &dyn PathWorld;
}

pub struct Ctx<'a> {
    pub s: &'a MobIn,
    pub out: &'a mut MobOut,
    pub world: &'a dyn MobWorld,
    pub nav: &'a mut Navigation,
    pub look: &'a mut LookControl,
    pub rng: &'a mut Random,
    pub target: &'a mut i32,
    pub kind: i32,
}

impl<'a> Ctx<'a> {
    pub fn view(&self, slot: i32) -> Option<EntityView> {
        let v = match slot {
            TARGET_PLAYER => self.s.player,
            TARGET_ATTACKER => self.s.attacker,
            _ => return None,
        };
        if v.usable() {
            Some(v)
        } else {
            None
        }
    }

    pub fn target_view(&self) -> Option<EntityView> {
        self.view(*self.target)
    }

    pub fn set_target(&mut self, slot: i32) {
        *self.target = slot;
    }

    pub fn clear_target(&mut self) {
        *self.target = TARGET_NONE;
    }

    pub fn is_baby(&self) -> bool {
        is_animal(self.kind) && self.s.age < 0
    }

    pub fn dist_sq(&self, v: &EntityView) -> f32 {
        let dx = self.s.x - v.x;
        let dy = self.s.y - v.y;
        let dz = self.s.z - v.z;
        dx * dx + dy * dy + dz * dz
    }

    pub fn dist(&self, v: &EntityView) -> f32 {
        sqrtf(self.dist_sq(v))
    }

    /// How much the mob would like to stand on that block. Animals go for grass
    /// and light, monsters for the dark.
    pub fn walk_target_value(&self, x: i32, y: i32, z: i32) -> f32 {
        if is_animal(self.kind) {
            if self.world.block(x, y - 1, z) == blocks::GRASS as i32 {
                return 10.0;
            }
            return self.world.brightness(x, y, z) - 0.5;
        }
        0.5 - self.world.brightness(x, y, z)
    }

    pub fn move_to(&mut self, x: f32, y: f32, z: f32, speed: f32) -> bool {
        self.nav.move_to(self.s, self.world.as_path(), x, y, z, speed)
    }

    pub fn move_to_block(&mut self, x: i32, y: i32, z: i32, speed: f32) -> bool {
        self.move_to(x as f32 + 0.5, y as f32 + 0.5, z as f32 + 0.5, speed)
    }

    pub fn move_to_entity(&mut self, v: &EntityView, speed: f32) -> bool {
        self.move_to(v.x, v.bb_y0, v.z, speed)
    }

    pub fn speed(&self, mul: f32) -> f32 {
        self.s.run_speed * mul
    }

    pub fn look_at_entity(&mut self, v: &EntityView, yaw_step: f32, pitch_step: f32) {
        self.look.set_look_at(v.x, v.eye_y(), v.z, yaw_step, pitch_step);
    }
}
