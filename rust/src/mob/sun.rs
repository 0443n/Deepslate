// Daylight, ported from Monster::updateSunburn and Pumpkin's flee_sun.

use super::ctx::{Ctx, MobWorld};
use super::goal::{Goal, MOVE};
use super::{MobIn, MobOut};
use crate::mth::floor;
use crate::random::Random;

/// Below this the sun is not strong enough to catch anything alight.
const BURN_BRIGHTNESS: f32 = 0.5;
const BURN_SECONDS: i32 = 8;
const SMOKE_PUFFS: i32 = 2;

/// Where Entity::getBrightness samples, two thirds up the box rather than at
/// the feet.
fn brightness_at(s: &MobIn, world: &dyn MobWorld) -> f32 {
    let y = floor(s.y + s.bb_height * 0.66);
    world.brightness(floor(s.x), y, floor(s.z))
}

/// Runs on odd ticks only, which is what the C++ sunburn counter did.
pub fn burn(s: &MobIn, world: &dyn MobWorld, rng: &mut Random, out: &mut MobOut) {
    if s.fire_immune != 0 || s.is_day == 0 {
        return;
    }
    let br = brightness_at(s, world);
    if br <= BURN_BRIGHTNESS {
        return;
    }
    if !world.can_see_sky(floor(s.x), floor(s.y), floor(s.z)) {
        return;
    }
    // Brighter light catches sooner, so a mob under a thin canopy smoulders
    // rather than lighting up at once.
    if rng.next_float() * 3.5 >= br - 0.4 {
        return;
    }
    out.ignite = BURN_SECONDS;
    out.smoke = SMOKE_PUFFS;
}

/// Sends a burning mob looking for shade instead of standing in the open until
/// it dies. Pumpkin's FleeSunGoal, minus the helmet check, since nothing here
/// wears one.
pub struct FleeSunGoal {
    speed_mul: f32,
    want: [i32; 3],
}

impl FleeSunGoal {
    pub fn new(speed_mul: f32) -> FleeSunGoal {
        FleeSunGoal { speed_mul, want: [0; 3] }
    }

    fn find_shade(&mut self, c: &mut Ctx) -> bool {
        let (mx, my, mz) = (floor(c.s.x), floor(c.s.y), floor(c.s.z));
        for _ in 0..10 {
            let x = mx + c.rng.next_int_bound(21) - 10;
            let y = my + c.rng.next_int_bound(7) - 3;
            let z = mz + c.rng.next_int_bound(21) - 10;
            if c.world.can_see_sky(x, y, z) {
                continue;
            }
            if solid(c, x, y, z) || solid(c, x, y + 1, z) || !solid(c, x, y - 1, z) {
                continue;
            }
            self.want = [x, y, z];
            return true;
        }
        false
    }
}

fn solid(c: &Ctx, x: i32, y: i32, z: i32) -> bool {
    crate::pathfinder::shared_flag(c.world.block(x, y, z), crate::pathfinder::F_SOLID)
}

impl Goal for FleeSunGoal {
    fn name(&self) -> &'static str {
        "shade"
    }
    fn controls(&self) -> u8 {
        MOVE
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        // A mob that has found something to fight would rather burn.
        if *c.target != super::TARGET_NONE || c.s.on_fire <= 0 {
            return false;
        }
        self.find_shade(c)
    }

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        !c.nav.is_done()
    }

    fn start(&mut self, c: &mut Ctx) {
        let speed = c.speed(self.speed_mul);
        c.move_to_block(self.want[0], self.want[1], self.want[2], speed);
    }
}
