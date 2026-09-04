// The attack goals, ported from Pumpkin's melee_attack, ranged_attack,
// leap_at_target and the creeper's ignite goal.

use crate::mob::ctx::Ctx;
use crate::mob::goal::{goal_ticks, Goal, JUMP, LOOK, MOVE};
use crate::mob::EntityView;
use crate::newlib::sqrtf;

/// Walks the mob onto its target and swings when it is in reach.
pub struct MeleeAttackGoal {
    speed_mul: f32,
    follow_unseen: bool,
    countdown: i32,
    last: (f32, f32, f32),
}

impl MeleeAttackGoal {
    pub fn new(speed_mul: f32, follow_unseen: bool) -> MeleeAttackGoal {
        MeleeAttackGoal { speed_mul, follow_unseen, countdown: 0, last: (0.0, 0.0, 0.0) }
    }

    fn reach_sq(c: &Ctx, target: &EntityView) -> f32 {
        let reach = c.s.bb_width * 2.0;
        reach * reach + target.width
    }
}

impl Goal for MeleeAttackGoal {
    fn name(&self) -> &'static str {
        "melee"
    }
    fn controls(&self) -> u8 {
        MOVE | LOOK
    }
    fn should_run_every_tick(&self) -> bool {
        true
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        let t = match c.target_view() {
            Some(t) => t,
            None => return false,
        };
        // Vanilla keeps a floor under the chase speed so a slow mob is still
        // worth running this goal for.
        let speed = c.speed(self.speed_mul).max(0.23);
        c.move_to_entity(&t, speed)
    }

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        if c.target_view().is_none() {
            return false;
        }
        self.follow_unseen || !c.nav.is_done()
    }

    fn start(&mut self, _c: &mut Ctx) {
        self.countdown = 0;
        self.last = (0.0, 0.0, 0.0);
    }

    fn stop(&mut self, c: &mut Ctx) {
        c.nav.stop();
    }

    fn tick(&mut self, c: &mut Ctx) {
        let t = match c.target_view() {
            Some(t) => t,
            None => return,
        };

        c.look_at_entity(&t, 30.0, 30.0);

        self.countdown -= 1;
        let dx = t.x - self.last.0;
        let dy = t.y - self.last.1;
        let dz = t.z - self.last.2;
        let moved = dx * dx + dy * dy + dz * dz >= 1.0;
        if self.countdown <= 0 && (moved || c.rng.next_int_bound(20) == 0) {
            self.last = (t.x, t.y, t.z);
            let speed = c.speed(self.speed_mul).max(0.23);
            c.move_to_entity(&t, speed);

            let d = c.dist_sq(&t);
            self.countdown = 4 + c.rng.next_int_bound(7);
            if d > 1024.0 {
                self.countdown += 10;
            } else if d > 256.0 {
                self.countdown += 5;
            }
        }

        if c.s.attack_time > 0 {
            return;
        }
        if c.dist_sq(&t) > Self::reach_sq(c, &t) {
            return;
        }
        // Keeps a mob from reaching through a floor or a ceiling.
        if t.bb_y1 <= c.s.bb_y0 || t.bb_y0 >= c.s.bb_y0 + c.s.bb_height {
            return;
        }
        c.out.attack = 1;
    }
}

/// Holds range and shoots, closing the gap when the shot is not on.
pub struct RangedAttackGoal {
    speed_mul: f32,
    interval: i32,
    range_sq: f32,
    cooldown: i32,
    seen_ticks: i32,
}

impl RangedAttackGoal {
    pub fn new(speed_mul: f32, interval: i32, range: f32) -> RangedAttackGoal {
        RangedAttackGoal {
            speed_mul,
            interval,
            range_sq: range * range,
            cooldown: 0,
            seen_ticks: 0,
        }
    }
}

impl Goal for RangedAttackGoal {
    fn name(&self) -> &'static str {
        "bow"
    }
    fn controls(&self) -> u8 {
        MOVE | LOOK
    }
    fn should_run_every_tick(&self) -> bool {
        true
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        c.target_view().is_some()
    }

    fn stop(&mut self, c: &mut Ctx) {
        self.seen_ticks = 0;
        self.cooldown = 0;
        c.nav.stop();
    }

    fn tick(&mut self, c: &mut Ctx) {
        let t = match c.target_view() {
            Some(t) => t,
            None => return,
        };

        let dist_sq = c.dist_sq(&t);
        let seen = t.can_see != 0;
        if seen {
            self.seen_ticks += 1;
        } else {
            self.seen_ticks = 0;
        }

        if dist_sq > self.range_sq || self.seen_ticks < 5 {
            let speed = c.speed(self.speed_mul);
            c.move_to_entity(&t, speed);
        } else {
            c.nav.stop();
        }

        c.look_at_entity(&t, 30.0, 30.0);

        if self.cooldown > 0 {
            self.cooldown -= 1;
            return;
        }
        if !seen || dist_sq > self.range_sq {
            return;
        }
        c.out.ranged = 1;
        c.out.ranged_dist = sqrtf(dist_sq);
        self.cooldown = self.interval;
    }
}

/// The spider pounce.
pub struct LeapAtTargetGoal {
    height: f32,
}

impl LeapAtTargetGoal {
    pub fn new(height: f32) -> LeapAtTargetGoal {
        LeapAtTargetGoal { height }
    }
}

impl Goal for LeapAtTargetGoal {
    fn name(&self) -> &'static str {
        "leap"
    }
    fn controls(&self) -> u8 {
        MOVE | JUMP
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        if c.s.on_ground == 0 {
            return false;
        }
        let t = match c.target_view() {
            Some(t) => t,
            None => return false,
        };
        let d = c.dist_sq(&t);
        if d < 4.0 || d > 16.0 {
            return false;
        }
        c.rng.next_int_bound(goal_ticks(5).max(1)) == 0
    }

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        c.s.on_ground == 0
    }

    fn start(&mut self, c: &mut Ctx) {
        let t = match c.target_view() {
            Some(t) => t,
            None => return,
        };
        let dx = t.x - c.s.x;
        let dz = t.z - c.s.z;
        let d = sqrtf(dx * dx + dz * dz);
        if d < 1e-4 {
            return;
        }
        c.out.set_vel = 1;
        c.out.xd = (dx / d) * 0.5 * 0.8 + c.s.xd * 0.2;
        c.out.zd = (dz / d) * 0.5 * 0.8 + c.s.zd * 0.2;
        c.out.yd = self.height;
    }
}

/// Holds ground next to the player and winds the fuse. The counting itself
/// stays in Creeper::tick so a lit creeper keeps going without the goal.
pub struct SwellGoal {
    lit: bool,
}

impl SwellGoal {
    pub fn new() -> SwellGoal {
        SwellGoal { lit: false }
    }
}

impl Goal for SwellGoal {
    fn name(&self) -> &'static str {
        "swell"
    }
    fn controls(&self) -> u8 {
        MOVE
    }
    fn should_run_every_tick(&self) -> bool {
        true
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        match c.target_view() {
            Some(t) => c.dist_sq(&t) < 9.0,
            None => false,
        }
    }

    fn should_continue(&mut self, _c: &mut Ctx) -> bool {
        self.lit
    }

    fn start(&mut self, c: &mut Ctx) {
        c.nav.stop();
        self.lit = true;
        c.out.swell_dir = 1;
    }

    fn stop(&mut self, c: &mut Ctx) {
        self.lit = false;
        c.out.swell_dir = -1;
    }

    fn tick(&mut self, c: &mut Ctx) {
        let keep = match c.target_view() {
            Some(t) => c.dist_sq(&t) <= 49.0 && t.can_see != 0,
            None => false,
        };
        self.lit = keep;
        c.out.swell_dir = if keep { 1 } else { -1 };
    }
}
