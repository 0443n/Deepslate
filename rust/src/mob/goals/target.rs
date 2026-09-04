// Target picking, ported from Pumpkin's track_target, active_target and revenge.

use crate::mob::ctx::Ctx;
use crate::mob::goal::{goal_ticks, Goal, TARGET};
use crate::mob::{TARGET_ATTACKER, TARGET_NONE, TARGET_PLAYER};

/// Turns on whoever landed the last hit. Keyed off when the mob was hurt rather
/// than off consuming the attacker, so a second hit renews the grudge.
pub struct HurtByTargetGoal {
    seen_hurt: i32,
    unseen_ticks: i32,
    /// Blocks to call the neighbours in from, 0 for a mob that fights alone.
    alert_range: i32,
}

const REVENGE_UNSEEN_LIMIT: i32 = 300;

impl HurtByTargetGoal {
    pub fn new() -> HurtByTargetGoal {
        HurtByTargetGoal { seen_hurt: 0, unseen_ticks: 0, alert_range: 0 }
    }

    /// Vanilla's setAlertOthers, which is what turns one pig zombie into a mob.
    pub fn alerting(range: i32) -> HurtByTargetGoal {
        HurtByTargetGoal { seen_hurt: 0, unseen_ticks: 0, alert_range: range }
    }
}

impl Goal for HurtByTargetGoal {
    fn name(&self) -> &'static str {
        "revenge"
    }
    fn controls(&self) -> u8 {
        TARGET
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        if c.s.last_hurt_time == self.seen_hurt {
            return false;
        }
        c.s.attacker.usable()
    }

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        if *c.target != TARGET_ATTACKER {
            return false;
        }
        let t = match c.target_view() {
            Some(t) => t,
            None => return false,
        };
        if c.s.gate_keep_target == 0 {
            return false;
        }
        if c.dist(&t) > 32.0 {
            return false;
        }
        if t.can_see != 0 {
            self.unseen_ticks = 0;
        } else {
            self.unseen_ticks += 1;
            if self.unseen_ticks > goal_ticks(REVENGE_UNSEEN_LIMIT) {
                return false;
            }
        }
        true
    }

    fn start(&mut self, c: &mut Ctx) {
        self.seen_hurt = c.s.last_hurt_time;
        self.unseen_ticks = 0;
        c.out.alert_others = self.alert_range;
        c.set_target(TARGET_ATTACKER);
    }

    fn stop(&mut self, c: &mut Ctx) {
        if *c.target == TARGET_ATTACKER {
            c.set_target(TARGET_NONE);
        }
    }
}

/// The only target worth having in a single player world is the player.
pub struct NearestPlayerTargetGoal {
    range: f32,
    reciprocal_chance: i32,
    unseen_ticks: i32,
}

const TARGET_UNSEEN_LIMIT: i32 = 60;

impl NearestPlayerTargetGoal {
    pub fn new(range: f32) -> NearestPlayerTargetGoal {
        NearestPlayerTargetGoal {
            range,
            reciprocal_chance: goal_ticks(10),
            unseen_ticks: 0,
        }
    }
}

impl Goal for NearestPlayerTargetGoal {
    fn name(&self) -> &'static str {
        "tplayer"
    }
    fn controls(&self) -> u8 {
        TARGET
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool {
        if *c.target != TARGET_NONE {
            return false;
        }
        if c.s.gate_can_attack == 0 {
            return false;
        }
        // Pumpkin only scans on a fraction of the ticks, which is what keeps a
        // mob from locking on the instant a player steps into range.
        if c.rng.next_int_bound(self.reciprocal_chance.max(1)) != 0 {
            return false;
        }
        let p = match c.view(TARGET_PLAYER) {
            Some(p) => p,
            None => return false,
        };
        // Creative is not worth chasing, extra carries the held item and the
        // gate above already asked the mob whether it wants this fight.
        if c.dist(&p) > self.range {
            return false;
        }
        p.can_see != 0
    }

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        if *c.target != TARGET_PLAYER {
            return false;
        }
        let t = match c.target_view() {
            Some(t) => t,
            None => return false,
        };
        if c.s.gate_keep_target == 0 {
            return false;
        }
        if c.dist(&t) > self.range * 2.0 {
            return false;
        }
        if t.can_see != 0 {
            self.unseen_ticks = 0;
        } else {
            self.unseen_ticks += 1;
            if self.unseen_ticks > goal_ticks(TARGET_UNSEEN_LIMIT) {
                return false;
            }
        }
        true
    }

    fn start(&mut self, c: &mut Ctx) {
        self.unseen_ticks = 0;
        c.set_target(TARGET_PLAYER);
    }

    fn stop(&mut self, c: &mut Ctx) {
        if *c.target == TARGET_PLAYER {
            c.set_target(TARGET_NONE);
        }
    }
}
