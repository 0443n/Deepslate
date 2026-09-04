// A pig on flat grass, ticked on the host. Not a parity vector, a smoke test
// that the goals actually run and ask the mob to walk somewhere.

use deepslate_gen::mob::ctx::MobWorld;
use deepslate_gen::mob::{EntityView, MobAi, MobIn, MobOut};
use deepslate_gen::pathfinder::PathWorld;

const GROUND: i32 = 63;

struct Flat;

impl PathWorld for Flat {
    fn block(&self, _x: i32, y: i32, _z: i32) -> i32 {
        if y <= GROUND {
            deepslate_gen::blocks::GRASS as i32
        } else {
            0
        }
    }
    fn data(&self, _x: i32, _y: i32, _z: i32) -> i32 {
        0
    }
}

impl MobWorld for Flat {
    fn block(&self, x: i32, y: i32, z: i32) -> i32 {
        PathWorld::block(self, x, y, z)
    }
    fn brightness(&self, _x: i32, y: i32, _z: i32) -> f32 {
        if y > GROUND {
            1.0
        } else {
            0.0
        }
    }
    fn can_see_sky(&self, _x: i32, y: i32, _z: i32) -> bool {
        y > GROUND
    }
    fn as_path(&self) -> &dyn PathWorld {
        self
    }
}

fn flags() -> [u8; 256] {
    let mut f = [0u8; 256];
    for id in 1..256usize {
        f[id] = deepslate_gen::pathfinder::F_SOLID;
    }
    f[0] = 0;
    f[deepslate_gen::blocks::WATER as usize] = deepslate_gen::pathfinder::F_WATER;
    f[deepslate_gen::blocks::CALM_WATER as usize] = deepslate_gen::pathfinder::F_WATER;
    f
}

fn snapshot(x: f32, z: f32, y_rot: f32, y_body_rot: f32) -> MobIn {
    let y = GROUND as f32 + 1.0;
    MobIn {
        kind: deepslate_gen::mob::KIND_PIG,
        age: 0,
        no_action_time: 0,
        flee_time: 0,
        attack_time: 0,
        health: 10,
        on_fire: 0,
        in_water: 0,
        in_lava: 0,
        on_ground: 1,
        horiz_collision: 0,
        last_hurt_time: 0,
        gate_can_attack: 0,
        gate_keep_target: 0,
        target_slot: 0,
        is_day: 1,
        fire_immune: 0,
        x,
        y,
        z,
        bb_x0: x - 0.45,
        bb_y0: y,
        bb_z0: z - 0.45,
        bb_width: 0.9,
        bb_height: 0.9,
        head_height: 0.62,
        y_rot,
        x_rot: 0.0,
        y_body_rot,
        xd: 0.0,
        yd: 0.0,
        zd: 0.0,
        run_speed: 0.7,
        player: EntityView {
            valid: 1,
            alive: 1,
            can_see: 1,
            extra: 0,
            x: x + 4.0,
            y,
            z: z + 4.0,
            bb_y0: y,
            bb_y1: y + 1.8,
            head: 1.62,
            width: 0.6,
        },
        attacker: EntityView::default(),
        parent: EntityView::default(),
    }
}

#[test]
fn pig_walks_and_looks() {
    deepslate_gen::pathfinder::shared_init(flags());
    let mut ai = MobAi::new(deepslate_gen::mob::KIND_PIG, 47, 296);

    let (mut x, mut z, mut y_rot) = (100.5f32, 100.5f32, 0.0f32);
    let mut y_body_rot = 0.0f32;
    let mut moved_ticks = 0;
    let mut turned = 0;
    let mut head_off = 0;
    let mut buf = [0u8; 96];

    for tick in 0..600 {
        let s = snapshot(x, z, y_rot, y_body_rot);
        let mut out = MobOut::default();
        ai.tick(&s, &Flat, &mut out);

        if out.yya != 0.0 {
            moved_ticks += 1;
            // Crude stand in for Mob::travel, enough to make the mob advance.
            let rad = (y_body_rot + 90.0).to_radians();
            let step = out.yya * 0.1;
            x += rad.cos() * step;
            z += rad.sin() * step;
        }
        if (out.y_rot - y_body_rot).abs() > 0.01 {
            turned += 1;
        }
        if (out.y_head_rot - out.y_rot).abs() > 1.0 {
            head_off += 1;
        }
        y_body_rot = out.y_rot;
        y_rot = out.y_head_rot;

        if tick % 60 == 0 {
            let n = ai.describe(&mut buf) as usize;
            let names = std::str::from_utf8(&buf[..n]).unwrap_or("?");
            let (i, len) = ai.nav_progress();
            println!(
                "t{:<4} goals=[{}] yya={:.2} yRot={:6.1} nav={}/{} pos=({:.1},{:.1})",
                tick, names, out.yya, y_rot, i, len, x, z
            );
        }
    }

    println!("moved on {moved_ticks}/600 ticks, turned on {turned}/600, head off body {head_off}/600");
    assert!(moved_ticks > 0, "the pig never asked to move in 600 ticks");
    assert!(turned > 0, "the pig never turned in 600 ticks");
    assert!(head_off > 0, "the pig's head never left the body line in 600 ticks");
}

/// The herd call that PigZombie::hurt used to make with its own box scan.
#[test]
fn a_hurt_pig_zombie_calls_the_herd_and_a_zombie_does_not() {
    deepslate_gen::pathfinder::shared_init(flags());

    let alert_after_hurt = |kind: i32| {
        let mut ai = MobAi::new(kind, 11, 0);
        let mut best = 0;
        for _ in 0..40 {
            let mut s = snapshot(100.5, 100.5, 0.0, 0.0);
            // The attacker slot is the player, standing where the snapshot put it.
            s.attacker = s.player;
            s.last_hurt_time = 1;
            s.gate_keep_target = 1;
            let mut out = MobOut::default();
            ai.tick(&s, &Flat, &mut out);
            best = best.max(out.alert_others);
        }
        best
    };

    assert_eq!(alert_after_hurt(deepslate_gen::mob::KIND_PIG_ZOMBIE), 12);
    assert_eq!(alert_after_hurt(deepslate_gen::mob::KIND_ZOMBIE), 0);
}
