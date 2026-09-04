// The goals every mob carries, ported from Pumpkin's swim, wander_around,
// look_at_entity, look_around and escape_danger.

use crate::mob::ctx::Ctx;
use crate::mob::goal::{goal_ticks, Goal, JUMP, LOOK, MOVE};
use crate::mob::TARGET_PLAYER;
use crate::mth::floor;
use crate::newlib::{cosf, sinf};

/// Keeps the mob's head above water instead of walking it along the bottom.
pub struct FloatGoal;

impl FloatGoal {
    pub fn new() -> FloatGoal {
        FloatGoal
    }
}

impl Goal for FloatGoal {
    fn name(&self) -> &'static str {
        "float"
    }
    fn controls(&self) -> u8 {
        JUMP
    }
    fn should_run_every_tick(&self) -> bool {
        true
    }
    fn can_start(&mut self, c: &mut Ctx) -> bool {
        c.s.in_water != 0 || c.s.in_lava != 0
    }
    fn tick(&mut self, c: &mut Ctx) {
        if c.rng.next_float() < 0.8 {
            c.out.jumping = 1;
        }
    }
}

/// Wanders to the most attractive spot within reach, scored by the mob's own
/// idea of what a good block to stand on is.
pub struct StrollGoal {
    speed_mul: f32,
    interval: i32,
    want: [i32; 3],
}

impl StrollGoal {
    pub fn new(speed_mul: f32, interval: i32) -> StrollGoal {
        StrollGoal { speed_mul, interval: goal_ticks(interval), want: [0; 3] }
    }

    /// Vanilla strolls with WaterAvoidingRandomStrollGoal everywhere, which is
    /// why animals wander around a pond instead of straight through it.
    fn wet(c: &Ctx, x: i32, y: i32, z: i32) -> bool {
        crate::pathfinder::shared_flag(c.world.block(x, y, z), crate::pathfinder::F_WATER)
            || crate::pathfinder::shared_flag(
                c.world.block(x, y - 1, z),
                crate::pathfinder::F_WATER,
            )
    }
}

impl Goal for StrollGoal {
    fn name(&self) -> &'static str {
        "stroll"
    }
    fn controls(&self) -> u8 {
        MOVE
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        // Vanilla parks a mob that has gone unattended, it is a despawn
        // candidate and strolling would keep resetting that.
        if c.s.no_action_time >= 100 {
            return false;
        }
        if c.rng.next_int_bound(self.interval) != 0 {
            return false;
        }
        let mut best = -99999.0f32;
        let mut found = false;
        for _ in 0..10 {
            let x = floor(c.s.x) + c.rng.next_int_bound(13) - 6;
            let y = floor(c.s.y) + c.rng.next_int_bound(7) - 3;
            let z = floor(c.s.z) + c.rng.next_int_bound(13) - 6;
            if Self::wet(c, x, y, z) {
                continue;
            }
            let value = c.walk_target_value(x, y, z);
            if value > best {
                best = value;
                self.want = [x, y, z];
                found = true;
            }
        }
        found
    }

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        !c.nav.is_done()
    }

    fn start(&mut self, c: &mut Ctx) {
        let speed = c.speed(self.speed_mul);
        c.move_to_block(self.want[0], self.want[1], self.want[2], speed);
    }
}

/// Watches the player for a while when they come close.
pub struct LookAtPlayerGoal {
    range: f32,
    probability: f32,
    look_time: i32,
}

impl LookAtPlayerGoal {
    pub fn new(range: f32, probability: f32) -> LookAtPlayerGoal {
        LookAtPlayerGoal { range, probability, look_time: 0 }
    }
}

impl Goal for LookAtPlayerGoal {
    fn name(&self) -> &'static str {
        "lookplr"
    }
    fn controls(&self) -> u8 {
        LOOK
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        if c.rng.next_float() >= self.probability {
            return false;
        }
        match c.view(TARGET_PLAYER) {
            Some(p) => c.dist(&p) <= self.range,
            None => false,
        }
    }

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        if self.look_time <= 0 {
            return false;
        }
        match c.view(TARGET_PLAYER) {
            Some(p) => c.dist(&p) <= self.range,
            None => false,
        }
    }

    fn start(&mut self, c: &mut Ctx) {
        self.look_time = goal_ticks(40 + c.rng.next_int_bound(40));
    }

    fn stop(&mut self, _c: &mut Ctx) {
        self.look_time = 0;
    }

    fn tick(&mut self, c: &mut Ctx) {
        self.look_time -= 1;
        if let Some(p) = c.view(TARGET_PLAYER) {
            c.look_at_entity(&p, 10.0, 40.0);
        }
    }
}

/// Idle head turning, so a mob standing still is not a statue.
pub struct RandomLookAroundGoal {
    rel_yaw: f32,
    look_time: i32,
}

impl RandomLookAroundGoal {
    pub fn new() -> RandomLookAroundGoal {
        RandomLookAroundGoal { rel_yaw: 0.0, look_time: 0 }
    }
}

impl Goal for RandomLookAroundGoal {
    fn name(&self) -> &'static str {
        "lookrnd"
    }
    fn controls(&self) -> u8 {
        // Pumpkin claims MOVE here too, which is what stops the mob strolling
        // off mid glance.
        MOVE | LOOK
    }
    /// The one look goal that runs on odd ticks as well, so the head sweeps
    /// smoothly instead of stepping every other frame.
    fn should_run_every_tick(&self) -> bool {
        true
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        c.rng.next_float() < 0.02
    }

    fn should_continue(&mut self, _c: &mut Ctx) -> bool {
        self.look_time >= 0
    }

    fn start(&mut self, c: &mut Ctx) {
        self.rel_yaw = (c.rng.next_float() - 0.5) * 2.0 * core::f32::consts::PI;
        self.look_time = 20 + c.rng.next_int_bound(20);
    }

    fn tick(&mut self, c: &mut Ctx) {
        self.look_time -= 1;
        let d = 8.0;
        let (x, y, z) = (
            c.s.x + cosf(self.rel_yaw) * d,
            c.s.y + c.s.head_height,
            c.s.z + sinf(self.rel_yaw) * d,
        );
        c.look.set_look_at(x, y, z, 10.0, 40.0);
    }
}

/// Runs away for a while after being hurt or set alight.
pub struct PanicGoal {
    speed_mul: f32,
    want: [i32; 3],
}

impl PanicGoal {
    pub fn new(speed_mul: f32) -> PanicGoal {
        PanicGoal { speed_mul, want: [0; 3] }
    }

    fn find_position(&mut self, c: &mut Ctx) -> bool {
        for _ in 0..10 {
            let x = floor(c.s.x) + c.rng.next_int_bound(11) - 5;
            let y = floor(c.s.y) + c.rng.next_int_bound(5) - 2;
            let z = floor(c.s.z) + c.rng.next_int_bound(11) - 5;
            if x == floor(c.s.x) && z == floor(c.s.z) {
                continue;
            }
            if c.world.block(x, y - 1, z) == 0 {
                continue;
            }
            self.want = [x, y, z];
            return true;
        }
        false
    }
}

impl Goal for PanicGoal {
    fn name(&self) -> &'static str {
        "panic"
    }
    fn controls(&self) -> u8 {
        MOVE
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        if c.s.flee_time <= 0 && c.s.on_fire <= 0 {
            return false;
        }
        self.find_position(c)
    }

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        !c.nav.is_done()
    }

    fn start(&mut self, c: &mut Ctx) {
        let speed = c.speed(self.speed_mul);
        c.move_to_block(self.want[0], self.want[1], self.want[2], speed);
    }
}
