//! The mob AI, ported from Pumpkin's entity/ai over Deepslate's own snapshot
//! boundary. C++ keeps entity storage, physics, collision and rendering, builds
//! a MobIn each tick and applies the MobOut that comes back.

pub mod ctx;
pub mod goal;
pub mod goals;
pub mod look;
pub mod nav;
pub mod sun;

use crate::random::Random;
use alloc::boxed::Box;
use ctx::Ctx;
use goal::GoalSelector;
use look::LookControl;
use nav::Navigation;

pub const KIND_PIG: i32 = 0;
pub const KIND_COW: i32 = 1;
pub const KIND_CHICKEN: i32 = 2;
pub const KIND_SHEEP: i32 = 3;
pub const KIND_ZOMBIE: i32 = 4;
pub const KIND_PIG_ZOMBIE: i32 = 5;
pub const KIND_SKELETON: i32 = 6;
pub const KIND_CREEPER: i32 = 7;
pub const KIND_SPIDER: i32 = 8;

/// How far a pig zombie's grudge carries to its neighbours.
const ALERT_RANGE: i32 = 12;

/// Which entity the mob is fighting. The id itself stays in C++, this only says
/// which of the snapshot's views it came from.
pub const TARGET_NONE: i32 = 0;
pub const TARGET_PLAYER: i32 = 1;
pub const TARGET_ATTACKER: i32 = 2;

/// One entity as the AI sees it. `extra` carries the held item for the player
/// and is unused for the others.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct EntityView {
    pub valid: i32,
    pub alive: i32,
    pub can_see: i32,
    pub extra: i32,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub bb_y0: f32,
    pub bb_y1: f32,
    pub head: f32,
    pub width: f32,
}

impl EntityView {
    pub fn usable(&self) -> bool {
        self.valid != 0 && self.alive != 0
    }
    pub fn eye_y(&self) -> f32 {
        self.y + self.head
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MobIn {
    pub kind: i32,
    pub age: i32,
    pub no_action_time: i32,
    pub flee_time: i32,
    pub attack_time: i32,
    pub health: i32,
    pub on_fire: i32,
    pub in_water: i32,
    pub in_lava: i32,
    pub on_ground: i32,
    pub horiz_collision: i32,
    pub last_hurt_time: i32,
    pub gate_can_attack: i32,
    pub gate_keep_target: i32,
    pub target_slot: i32,
    pub is_day: i32,
    /// Set for the mobs daylight cannot touch, which is the pig zombie here.
    pub fire_immune: i32,

    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub bb_x0: f32,
    pub bb_y0: f32,
    pub bb_z0: f32,
    pub bb_width: f32,
    pub bb_height: f32,
    pub head_height: f32,
    pub y_rot: f32,
    pub x_rot: f32,
    /// Where the body points. y_rot is the head, which the look control moves
    /// independently.
    pub y_body_rot: f32,
    pub xd: f32,
    pub yd: f32,
    pub zd: f32,
    pub run_speed: f32,

    pub player: EntityView,
    pub attacker: EntityView,
    pub parent: EntityView,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct MobOut {
    pub xxa: f32,
    pub yya: f32,
    pub y_rot: f32,
    pub x_rot: f32,
    pub jumping: i32,
    pub set_vel: i32,
    pub xd: f32,
    pub yd: f32,
    pub zd: f32,
    pub target_slot: i32,
    pub attack: i32,
    pub ranged: i32,
    pub ranged_dist: f32,
    /// -1 or 1 for the creeper fuse, 0 leaves whatever C++ has alone.
    pub swell_dir: i32,
    pub eat_tick: i32,
    /// 1 turns the grass under the mob into dirt and runs the ate() hook.
    pub eat_block: i32,
    /// Where the head points. Separate from y_rot, which steers the body, so a
    /// mob can watch the player while walking somewhere else.
    pub y_head_rot: f32,
    /// Seconds of fire to set, 0 leaves the mob alone.
    pub ignite: i32,
    /// Smoke puffs to throw off around the mob this tick.
    pub smoke: i32,
    /// Blocks to wake the mob's own kind within, 0 for the usual case.
    pub alert_others: i32,
}

pub struct MobAi {
    pub kind: i32,
    goals: GoalSelector,
    targets: GoalSelector,
    nav: Navigation,
    look: LookControl,
    rng: Random,
    target: i32,
    scan: i32,
}

fn is_animal(kind: i32) -> bool {
    kind <= KIND_SHEEP
}

impl MobAi {
    pub fn new(kind: i32, seed: i32, tempt_item: i32) -> MobAi {
        let mut ai = MobAi {
            kind,
            goals: GoalSelector::new(),
            targets: GoalSelector::new(),
            nav: Navigation::new(),
            look: LookControl::new(),
            // Per mob stream, so a litter that spawned together does not walk
            // the world in lockstep off one shared sequence.
            rng: Random::new(seed.wrapping_mul(0x9e37_79b9u32 as i32) ^ 0x5f3a_1c2d),
            target: TARGET_NONE,
            scan: 0,
        };
        if is_animal(kind) {
            ai.register_animal(tempt_item);
        } else {
            ai.register_monster();
        }
        ai
    }

    fn register_animal(&mut self, tempt_item: i32) {
        use goals::*;
        // Vanilla's own multipliers, per animal. A cow bolts, a pig trots.
        let (panic, tempt, parent) = match self.kind {
            KIND_COW => (2.0, 1.25, 1.25),
            KIND_CHICKEN => (1.4, 1.0, 1.1),
            KIND_SHEEP => (1.25, 1.1, 1.1),
            _ => (1.25, 1.2, 1.1),
        };

        self.goals.add(0, Box::new(FloatGoal::new()));
        self.goals.add(1, Box::new(PanicGoal::new(panic)));
        self.goals.add(3, Box::new(TemptGoal::new(tempt, tempt_item)));
        self.goals.add(4, Box::new(FollowParentGoal::new(parent)));

        // The sheep's meal sits above strolling, so the rest shift down with it.
        let base = if self.kind == KIND_SHEEP {
            self.goals.add(5, Box::new(EatBlockGoal::new()));
            6
        } else {
            5
        };
        self.goals.add(base, Box::new(StrollGoal::new(1.0, 120)));
        self.goals.add(base + 1, Box::new(LookAtPlayerGoal::new(6.0, 0.02)));
        self.goals.add(base + 2, Box::new(RandomLookAroundGoal::new()));
    }

    fn register_monster(&mut self) {
        use goals::*;
        self.goals.add(0, Box::new(FloatGoal::new()));
        match self.kind {
            KIND_CREEPER => {
                self.goals.add(2, Box::new(SwellGoal::new()));
                self.goals.add(3, Box::new(MeleeAttackGoal::new(1.0, false)));
            }
            KIND_SKELETON => {
                self.goals.add(2, Box::new(RangedAttackGoal::new(1.0, 60, 10.0)));
                // Vanilla gives the sun goals to skeletons alone. A zombie just
                // burns where it stands.
                self.goals.add(3, Box::new(sun::FleeSunGoal::new(1.0)));
            }
            KIND_SPIDER => {
                self.goals.add(3, Box::new(LeapAtTargetGoal::new(0.4)));
                self.goals.add(4, Box::new(MeleeAttackGoal::new(1.0, true)));
            }
            _ => {
                self.goals.add(2, Box::new(MeleeAttackGoal::new(1.0, false)));
            }
        }
        // Spiders and creepers stroll slower than the walking dead do.
        let stroll = if self.kind == KIND_SPIDER || self.kind == KIND_CREEPER { 0.8 } else { 1.0 };
        self.goals.add(5, Box::new(StrollGoal::new(stroll, 120)));
        self.goals.add(6, Box::new(LookAtPlayerGoal::new(8.0, 0.02)));
        self.goals.add(6, Box::new(RandomLookAroundGoal::new()));

        // Hurting one pig zombie is what brings the rest of them over.
        let revenge = if self.kind == KIND_PIG_ZOMBIE {
            HurtByTargetGoal::alerting(ALERT_RANGE)
        } else {
            HurtByTargetGoal::new()
        };
        self.targets.add(1, Box::new(revenge));
        self.targets.add(2, Box::new(NearestPlayerTargetGoal::new(16.0)));
    }

    pub fn tick(&mut self, s: &MobIn, world: &dyn ctx::MobWorld, out: &mut MobOut) {
        // C++ owns the target id, so a target it dropped has to be honoured here.
        if s.target_slot == TARGET_NONE {
            self.target = TARGET_NONE;
        }

        *out = MobOut::default();
        out.y_rot = s.y_body_rot;
        out.y_head_rot = s.y_rot;
        out.x_rot = s.x_rot;
        out.xd = s.xd;
        out.yd = s.yd;
        out.zd = s.zd;

        let full = (self.scan & 1) == 0;
        self.scan = self.scan.wrapping_add(1);

        {
            let MobAi { goals, targets, nav, look, rng, target, kind, .. } = self;
            let mut c = Ctx {
                s,
                out,
                world,
                nav,
                look,
                rng,
                target,
                kind: *kind,
            };
            targets.tick(&mut c, full);
            goals.tick(&mut c, full);
            c.nav.tick(s, c.out);
        }

        // The sunburn counter ran on alternate ticks, and the goal scan is the
        // alternation we already have.
        if !full && !is_animal(self.kind) {
            sun::burn(s, world, &mut self.rng, out);
        }

        out.target_slot = self.target;

        // NOTE: beta hopped at any wall it walked into, which is what left mobs
        // bouncing against a two block ledge forever. Only the path's own step
        // up asks for a jump now, the way vanilla and Pumpkin do it.
        self.look.tick(s, out);
    }

    /// One line of state for the console trace. `nav` is the node the mob is
    /// walking to out of how many the path has.
    pub fn describe(&self, buf: &mut [u8]) -> usize {
        fn put(buf: &mut [u8], at: &mut usize, bytes: &[u8]) {
            for &b in bytes {
                if *at < buf.len() {
                    buf[*at] = b;
                    *at += 1;
                }
            }
        }
        let mut at = 0usize;
        put(buf, &mut at, b"g=");
        self.goals.describe(buf, &mut at);
        put(buf, &mut at, b" t=");
        self.targets.describe(buf, &mut at);
        at
    }

    pub fn target_slot(&self) -> i32 {
        self.target
    }

    pub fn nav_progress(&self) -> (i32, i32) {
        self.nav.progress()
    }
}

// Pinned against the static asserts in src/world/entity/path_finder_mob.cpp.
const _: () = assert!(core::mem::size_of::<EntityView>() == 44);
const _: () = assert!(core::mem::size_of::<MobIn>() == 264);
const _: () = assert!(core::mem::size_of::<MobOut>() == 80);
