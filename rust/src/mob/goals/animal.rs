// The animal goals, ported from Pumpkin's tempt, eat_grass and follow_parent.

use crate::blocks;
use crate::mob::ctx::Ctx;
use crate::mob::goal::{goal_ticks, Goal, JUMP, LOOK, MOVE};
use crate::mob::TARGET_PLAYER;
use crate::mth::floor;

const TEMPT_RANGE: f32 = 10.0;
const TEMPT_STOP_DIST_SQ: f32 = 6.25;
/// How long a tempt path is kept before the search runs again.
const REPATH_TICKS: i32 = 10;

/// Follows the player while they hold the right item.
pub struct TemptGoal {
    speed_mul: f32,
    item_id: i32,
    cooldown: i32,
    repath: i32,
}

impl TemptGoal {
    pub fn new(speed_mul: f32, item_id: i32) -> TemptGoal {
        TemptGoal { speed_mul, item_id, cooldown: 0, repath: 0 }
    }
}

impl Goal for TemptGoal {
    fn name(&self) -> &'static str {
        "tempt"
    }
    fn controls(&self) -> u8 {
        MOVE | LOOK
    }
    fn should_run_every_tick(&self) -> bool {
        true
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        if self.cooldown > 0 {
            self.cooldown -= 1;
            return false;
        }
        // An empty hand reads as item 0, so a mob without a tempt item would
        // otherwise follow every player who is holding nothing.
        if self.item_id == 0 {
            return false;
        }
        match c.view(TARGET_PLAYER) {
            Some(p) => p.extra == self.item_id && c.dist(&p) < TEMPT_RANGE,
            None => false,
        }
    }

    fn stop(&mut self, c: &mut Ctx) {
        self.cooldown = 100;
        c.nav.stop();
    }

    fn start(&mut self, c: &mut Ctx) {
        c.nav.stop();
        self.repath = 0;
    }

    fn tick(&mut self, c: &mut Ctx) {
        let p = match c.view(TARGET_PLAYER) {
            Some(p) => p,
            None => return,
        };
        c.look_at_entity(&p, 30.0, 30.0);
        // Crowding the player looks wrong, so the animal stops just short.
        if c.dist_sq(&p) < TEMPT_STOP_DIST_SQ {
            c.nav.stop();
            return;
        }
        // A search costs more here than anywhere else in the tick, so the path
        // is only rebuilt a few times a second rather than on every one.
        if self.repath > 0 {
            self.repath -= 1;
            return;
        }
        self.repath = REPATH_TICKS;
        let speed = c.speed(self.speed_mul);
        c.move_to_entity(&p, speed);
    }
}

/// Grazes on the grass block underfoot, which is what regrows sheep wool.
pub struct EatBlockGoal {
    timer: i32,
}

const EAT_DURATION: i32 = 40;

/// What eat_block asks C++ to do, kept in step with applyIntent.
pub const EAT_NOTHING: i32 = 0;
pub const EAT_GRASS_BLOCK: i32 = 1;
pub const EAT_TALL_GRASS: i32 = 2;

impl EatBlockGoal {
    pub fn new() -> EatBlockGoal {
        EatBlockGoal { timer: 0 }
    }

    /// Vanilla looks for tall grass at the mob's own feet first, and only then
    /// for a grass block to scrape down to dirt.
    fn meal(c: &Ctx) -> i32 {
        let (x, y, z) = (floor(c.s.x), floor(c.s.y), floor(c.s.z));
        if c.world.block(x, y, z) == blocks::TALLGRASS as i32 {
            return EAT_TALL_GRASS;
        }
        if c.world.block(x, y - 1, z) == blocks::GRASS as i32 {
            return EAT_GRASS_BLOCK;
        }
        EAT_NOTHING
    }
}

impl Goal for EatBlockGoal {
    fn name(&self) -> &'static str {
        "eat"
    }
    fn controls(&self) -> u8 {
        MOVE | LOOK | JUMP
    }
    fn should_run_every_tick(&self) -> bool {
        true
    }
    /// The meal finishes even if something better comes along.
    fn can_stop(&self) -> bool {
        false
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        let odds = if c.is_baby() { 50 } else { 1000 };
        if c.rng.next_int_bound(odds) != 0 {
            return false;
        }
        Self::meal(c) != 0
    }

    fn should_continue(&mut self, _c: &mut Ctx) -> bool {
        self.timer > 0
    }

    fn start(&mut self, c: &mut Ctx) {
        self.timer = EAT_DURATION;
        c.nav.stop();
    }

    fn stop(&mut self, c: &mut Ctx) {
        self.timer = 0;
        c.out.eat_tick = 0;
    }

    fn tick(&mut self, c: &mut Ctx) {
        self.timer -= 1;
        c.out.eat_tick = self.timer;
        if self.timer == 4 {
            c.out.eat_block = Self::meal(c);
        }
    }
}

/// Keeps a baby animal near the nearest grown one of its own kind.
pub struct FollowParentGoal {
    speed_mul: f32,
    delay: i32,
}

const PARENT_MIN_DIST_SQ: f32 = 9.0;
const PARENT_MAX_DIST_SQ: f32 = 256.0;

impl FollowParentGoal {
    pub fn new(speed_mul: f32) -> FollowParentGoal {
        FollowParentGoal { speed_mul, delay: 0 }
    }
}

impl Goal for FollowParentGoal {
    fn name(&self) -> &'static str {
        "parent"
    }
    fn controls(&self) -> u8 {
        // Pumpkin claims nothing here, the goal only nudges the navigator and
        // lets a real MOVE goal take over whenever one wants to.
        0
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        if !c.is_baby() || !c.s.parent.usable() {
            return false;
        }
        c.dist_sq(&c.s.parent) > PARENT_MIN_DIST_SQ
    }

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        if !c.is_baby() || !c.s.parent.usable() {
            return false;
        }
        let d = c.dist_sq(&c.s.parent);
        d > PARENT_MIN_DIST_SQ && d < PARENT_MAX_DIST_SQ
    }

    fn start(&mut self, _c: &mut Ctx) {
        self.delay = 0;
    }

    fn tick(&mut self, c: &mut Ctx) {
        self.delay -= 1;
        if self.delay > 0 {
            return;
        }
        self.delay = goal_ticks(10);
        let parent = c.s.parent;
        let speed = c.speed(self.speed_mul);
        c.move_to_entity(&parent, speed);
    }
}
