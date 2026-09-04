//! A PSP-free testbed for the mob AI.
//!
//! Mirrors Entity::move, Mob::travel and PathfinderMob::buildSnapshot closely
//! enough that behaviour here matches the console, so goal changes can be judged
//! without a memory stick. Run with
//!   cargo run --release --target x86_64-unknown-linux-gnu --example mobsim

use deepslate_gen::blocks;
use deepslate_gen::mob::ctx::MobWorld;
use deepslate_gen::mob::{self, EntityView, MobAi, MobIn, MobOut};
use deepslate_gen::pathfinder::{PathWorld, F_SOLID, F_WATER};

const SX: usize = 96;
const SZ: usize = 96;
const SY: usize = 40;
const Y0: i32 = 48;

const AIR: u8 = 0;

// --- world ---------------------------------------------------------------

struct Grid {
    b: Vec<u8>,
}

impl Grid {
    fn new() -> Grid {
        Grid { b: vec![0; SX * SY * SZ] }
    }
    fn idx(x: i32, y: i32, z: i32) -> Option<usize> {
        let (xi, yi, zi) = (x, y - Y0, z);
        if xi < 0 || zi < 0 || yi < 0 {
            return None;
        }
        let (xi, yi, zi) = (xi as usize, yi as usize, zi as usize);
        if xi >= SX || yi >= SY || zi >= SZ {
            return None;
        }
        Some((yi * SZ + zi) * SX + xi)
    }
    fn get(&self, x: i32, y: i32, z: i32) -> u8 {
        Grid::idx(x, y, z).map_or(AIR, |i| self.b[i])
    }
    fn set(&mut self, x: i32, y: i32, z: i32, id: u8) {
        if let Some(i) = Grid::idx(x, y, z) {
            self.b[i] = id;
        }
    }
    fn solid(&self, x: i32, y: i32, z: i32) -> bool {
        let id = self.get(x, y, z);
        id != AIR && id != blocks::WATER && id != blocks::CALM_WATER && id != blocks::TALLGRASS
    }
    fn water(&self, x: i32, y: i32, z: i32) -> bool {
        let id = self.get(x, y, z);
        id == blocks::WATER || id == blocks::CALM_WATER
    }
}

impl PathWorld for Grid {
    fn block(&self, x: i32, y: i32, z: i32) -> i32 {
        self.get(x, y, z) as i32
    }
    fn data(&self, _x: i32, _y: i32, _z: i32) -> i32 {
        0
    }
}

impl MobWorld for Grid {
    fn block(&self, x: i32, y: i32, z: i32) -> i32 {
        self.get(x, y, z) as i32
    }
    fn brightness(&self, x: i32, y: i32, z: i32) -> f32 {
        // Daylight above ground, dark under it. Enough for walk_target_value.
        let mut yy = y;
        while yy < Y0 + SY as i32 {
            if self.solid(x, yy, z) {
                return 0.1;
            }
            yy += 1;
        }
        1.0
    }
    fn can_see_sky(&self, x: i32, y: i32, z: i32) -> bool {
        let mut yy = y + 1;
        while yy < Y0 + SY as i32 {
            if self.get(x, yy, z) != AIR {
                return false;
            }
            yy += 1;
        }
        true
    }
    fn as_path(&self) -> &dyn PathWorld {
        self
    }
}

/// Grass plain with a pond, a one block step and a two block wall - the three
/// bits of terrain that showed up the old navigator.
fn build() -> Grid {
    let mut g = Grid::new();
    for x in 0..SX as i32 {
        for z in 0..SZ as i32 {
            for y in Y0..63 {
                g.set(x, y, z, blocks::STONE);
            }
            g.set(x, 63, z, blocks::DIRT);
            g.set(x, 64, z, blocks::GRASS);
        }
    }
    // Pond, two deep so it is worth avoiding.
    for x in 14..30i32 {
        for z in 14..30i32 {
            let (dx, dz) = ((x - 22) as f32, (z - 22) as f32);
            if dx * dx + dz * dz <= 49.0 {
                g.set(x, 64, z, blocks::WATER);
                g.set(x, 63, z, blocks::WATER);
                g.set(x, 62, z, blocks::DIRT);
            }
        }
    }
    // One block step up over the east half.
    for x in 60..SX as i32 {
        for z in 0..SZ as i32 {
            g.set(x, 65, z, blocks::GRASS);
            g.set(x, 64, z, blocks::DIRT);
        }
    }
    // Tall grass over roughly a third of the plain, the meal sheep reach for
    // first. A cheap hash stands in for a noise field.
    for x in 0..SX as i32 {
        for z in 0..SZ as i32 {
            let y = if x >= 60 { 66 } else { 65 };
            if g.get(x, y - 1, z) == blocks::GRASS
                && (x.wrapping_mul(73) ^ z.wrapping_mul(179)) % 3 == 0
            {
                g.set(x, y, z, blocks::TALLGRASS);
            }
        }
    }
    // Shelter over the east plain, the only shade a burning mob can reach.
    for x in 80..88i32 {
        for z in 80..88i32 {
            g.set(x, 68, z, blocks::STONE);
        }
    }
    // Two block wall, the thing mobs used to bounce off forever.
    for z in 40..70i32 {
        g.set(45, 65, z, blocks::STONE);
        g.set(45, 66, z, blocks::STONE);
    }
    g
}

// --- physics -------------------------------------------------------------

const EPS: f32 = 1.0e-3;

#[derive(Clone, Copy)]
struct Aabb {
    x0: f32,
    y0: f32,
    z0: f32,
    x1: f32,
    y1: f32,
    z1: f32,
}

impl Aabb {
    fn moved(&self, dx: f32, dy: f32, dz: f32) -> Aabb {
        Aabb {
            x0: self.x0 + dx,
            y0: self.y0 + dy,
            z0: self.z0 + dz,
            x1: self.x1 + dx,
            y1: self.y1 + dy,
            z1: self.z1 + dz,
        }
    }
    fn expand(&self, dx: f32, dy: f32, dz: f32) -> Aabb {
        let mut r = *self;
        if dx < 0.0 { r.x0 += dx } else { r.x1 += dx }
        if dy < 0.0 { r.y0 += dy } else { r.y1 += dy }
        if dz < 0.0 { r.z0 += dz } else { r.z1 += dz }
        r
    }
    fn clip_x(&self, c: &Aabb, mut xa: f32) -> f32 {
        if c.y1 <= self.y0 + EPS || c.y0 >= self.y1 - EPS { return xa; }
        if c.z1 <= self.z0 + EPS || c.z0 >= self.z1 - EPS { return xa; }
        if xa > 0.0 && c.x1 <= self.x0 + EPS {
            let m = self.x0 - c.x1;
            if m < xa { xa = m }
        }
        if xa < 0.0 && c.x0 >= self.x1 - EPS {
            let m = self.x1 - c.x0;
            if m > xa { xa = m }
        }
        xa
    }
    fn clip_y(&self, c: &Aabb, mut ya: f32) -> f32 {
        if c.x1 <= self.x0 + EPS || c.x0 >= self.x1 - EPS { return ya; }
        if c.z1 <= self.z0 + EPS || c.z0 >= self.z1 - EPS { return ya; }
        if ya > 0.0 && c.y1 <= self.y0 + EPS {
            let m = self.y0 - c.y1;
            if m < ya { ya = m }
        }
        if ya < 0.0 && c.y0 >= self.y1 - EPS {
            let m = self.y1 - c.y0;
            if m > ya { ya = m }
        }
        ya
    }
    fn clip_z(&self, c: &Aabb, mut za: f32) -> f32 {
        if c.x1 <= self.x0 + EPS || c.x0 >= self.x1 - EPS { return za; }
        if c.y1 <= self.y0 + EPS || c.y0 >= self.y1 - EPS { return za; }
        if za > 0.0 && c.z1 <= self.z0 + EPS {
            let m = self.z0 - c.z1;
            if m < za { za = m }
        }
        if za < 0.0 && c.z0 >= self.z1 - EPS {
            let m = self.z1 - c.z0;
            if m > za { za = m }
        }
        za
    }
}

fn cubes(g: &Grid, b: &Aabb) -> Vec<Aabb> {
    let mut out = Vec::new();
    for x in b.x0.floor() as i32..=b.x1.floor() as i32 {
        for y in b.y0.floor() as i32..=b.y1.floor() as i32 {
            for z in b.z0.floor() as i32..=b.z1.floor() as i32 {
                if g.solid(x, y, z) {
                    out.push(Aabb {
                        x0: x as f32,
                        y0: y as f32,
                        z0: z as f32,
                        x1: x as f32 + 1.0,
                        y1: y as f32 + 1.0,
                        z1: z as f32 + 1.0,
                    });
                }
            }
        }
    }
    out
}

struct Body {
    x: f32,
    y: f32,
    z: f32,
    xd: f32,
    yd: f32,
    zd: f32,
    bb: Aabb,
    width: f32,
    height: f32,
    y_rot: f32,
    y_body_rot: f32,
    x_rot: f32,
    on_ground: bool,
    horiz: bool,
    foot_size: f32,
}

impl Body {
    fn new(x: f32, y: f32, z: f32, width: f32, height: f32) -> Body {
        let w = width / 2.0;
        Body {
            x,
            y,
            z,
            xd: 0.0,
            yd: 0.0,
            zd: 0.0,
            bb: Aabb { x0: x - w, y0: y, z0: z - w, x1: x + w, y1: y + height, z1: z + w },
            width,
            height,
            y_rot: 0.0,
            y_body_rot: 0.0,
            x_rot: 0.0,
            on_ground: false,
            horiz: false,
            foot_size: 0.5,
        }
    }

    fn sync(&mut self) {
        self.x = (self.bb.x0 + self.bb.x1) / 2.0;
        self.y = self.bb.y0;
        self.z = (self.bb.z0 + self.bb.z1) / 2.0;
    }

    /// Entity::move, including the footSize step up that lets a mob walk onto a
    /// one block rise without jumping.
    fn do_move(&mut self, g: &Grid, mut xa: f32, mut ya: f32, mut za: f32) {
        let (xa_org, ya_org, za_org) = (xa, ya, za);
        let bb_org = self.bb;
        let boxes = cubes(g, &self.bb.expand(xa, ya, za));

        for c in &boxes { ya = c.clip_y(&self.bb, ya) }
        self.bb = self.bb.moved(0.0, ya, 0.0);
        let og = self.on_ground || (ya_org != ya && ya_org < 0.0);

        for c in &boxes { xa = c.clip_x(&self.bb, xa) }
        self.bb = self.bb.moved(xa, 0.0, 0.0);
        for c in &boxes { za = c.clip_z(&self.bb, za) }
        self.bb = self.bb.moved(0.0, 0.0, za);

        if self.foot_size > 0.0 && og && (xa_org != xa || za_org != za) {
            let (xa_n, ya_n, za_n) = (xa, ya, za);
            let (mut xs, mut ys, mut zs) = (xa_org, self.foot_size, za_org);
            let normal = self.bb;
            self.bb = bb_org;
            let step = cubes(g, &self.bb.expand(xs, ys, zs));
            for c in &step { ys = c.clip_y(&self.bb, ys) }
            self.bb = self.bb.moved(0.0, ys, 0.0);
            for c in &step { xs = c.clip_x(&self.bb, xs) }
            self.bb = self.bb.moved(xs, 0.0, 0.0);
            for c in &step { zs = c.clip_z(&self.bb, zs) }
            self.bb = self.bb.moved(0.0, 0.0, zs);

            if xa_n * xa_n + za_n * za_n >= xs * xs + zs * zs {
                self.bb = normal;
                xa = xa_n;
                ya = ya_n;
                za = za_n;
            } else {
                xa = xs;
                ya += ys;
                za = zs;
            }
        }

        self.sync();
        self.horiz = xa_org != xa || za_org != za;
        self.on_ground = ya_org != ya && ya_org < 0.0;
        if ya_org != ya { self.yd = 0.0 }
        if xa_org != xa { self.xd = 0.0 }
        if za_org != za { self.zd = 0.0 }
    }

    fn move_relative(&mut self, xa: f32, za: f32, speed: f32) {
        let mut dist = (xa * xa + za * za).sqrt();
        if dist < 0.01 { return }
        if dist < 1.0 { dist = 1.0 }
        dist = speed / dist;
        let (xa, za) = (xa * dist, za * dist);
        let s = (self.y_body_rot * std::f32::consts::PI / 180.0).sin();
        let c = (self.y_body_rot * std::f32::consts::PI / 180.0).cos();
        self.xd += xa * c - za * s;
        self.zd += za * c + xa * s;
    }

    fn in_water(&self, g: &Grid) -> bool {
        let b = self.bb;
        for x in b.x0.floor() as i32..=b.x1.floor() as i32 {
            for y in (b.y0 - 0.4).floor() as i32..=b.y1.floor() as i32 {
                for z in b.z0.floor() as i32..=b.z1.floor() as i32 {
                    if g.water(x, y, z) { return true }
                }
            }
        }
        false
    }

    /// Mob::travel, the ground and water branches.
    fn travel(&mut self, g: &Grid, xs: f32, yf: f32) {
        if self.in_water(g) {
            self.move_relative(xs, yf, 0.02);
            let (xd, yd, zd) = (self.xd, self.yd, self.zd);
            self.do_move(g, xd, yd, zd);
            self.xd *= 0.80;
            self.yd *= 0.80;
            self.zd *= 0.80;
            self.yd -= 0.02;
            return;
        }
        let friction = if self.on_ground { 0.546 } else { 0.91 };
        let f3 = friction * friction * friction;
        let friction2 = (0.6 * 0.6 * 0.91 * 0.91 * 0.6 * 0.91) / f3;
        let speed = if self.on_ground { 0.1 * friction2 } else { 0.02 };
        self.move_relative(xs, yf, speed);
        let (xd, yd, zd) = (self.xd, self.yd, self.zd);
        self.do_move(g, xd, yd, zd);
        self.yd -= 0.08;
        self.yd *= 0.98;
        let hdrag = if self.on_ground { 0.546 } else { 0.91 };
        self.xd *= hdrag;
        self.zd *= hdrag;
    }
}

// --- actors --------------------------------------------------------------

struct Actor {
    body: Body,
    ai: MobAi,
    kind: i32,
    name: &'static str,
    age: i32,
    no_action_time: i32,
    flee_time: i32,
    /// Ticks of fire left, what Entity::setOnFire counts down.
    fire: i32,
    // stats
    dist: f32,
    jump_ticks: i32,
    collide_ticks: i32,
    wet_ticks: i32,
    eats: i32,
    tall_eats: i32,
    fire_ticks: i32,
    turn_ticks: i32,
    head_turn_ticks: i32,
    head_off: f32,
    ymin: f32,
    ymax: f32,
    cells: std::collections::HashSet<(i32, i32)>,
    goal_hist: std::collections::HashMap<String, i32>,
}

impl Actor {
    fn new(kind: i32, name: &'static str, x: f32, z: f32, y: f32, w: f32, h: f32, tempt: i32) -> Actor {
        Actor {
            body: Body::new(x, y, z, w, h),
            ai: MobAi::new(kind, (x as i32) * 31 + (z as i32) * 17 + kind, tempt),
            kind,
            name,
            age: 0,
            no_action_time: 0,
            flee_time: 0,
            fire: 0,
            dist: 0.0,
            jump_ticks: 0,
            collide_ticks: 0,
            wet_ticks: 0,
            eats: 0,
            tall_eats: 0,
            fire_ticks: 0,
            turn_ticks: 0,
            head_turn_ticks: 0,
            head_off: 0.0,
            ymin: 999.0,
            ymax: -999.0,
            cells: std::collections::HashSet::new(),
            goal_hist: std::collections::HashMap::new(),
        }
    }

    fn snapshot(&self, g: &Grid, player: &Body, held: i32) -> MobIn {
        let b = &self.body;
        let mut s = MobIn {
            kind: self.kind,
            age: self.age,
            no_action_time: self.no_action_time,
            flee_time: self.flee_time,
            attack_time: 0,
            health: 10,
            on_fire: self.fire,
            in_water: if b.in_water(g) { 1 } else { 0 },
            in_lava: 0,
            on_ground: if b.on_ground { 1 } else { 0 },
            horiz_collision: if b.horiz { 1 } else { 0 },
            last_hurt_time: 0,
            gate_can_attack: 1,
            gate_keep_target: 1,
            target_slot: 0,
            is_day: 1,
            fire_immune: 0,
            x: b.x,
            y: b.y,
            z: b.z,
            bb_x0: b.bb.x0,
            bb_y0: b.bb.y0,
            bb_z0: b.bb.z0,
            bb_width: b.width,
            bb_height: b.height,
            head_height: b.height * 0.85,
            y_rot: b.y_rot,
            x_rot: b.x_rot,
            y_body_rot: b.y_body_rot,
            xd: b.xd,
            yd: b.yd,
            zd: b.zd,
            run_speed: 0.7,
            player: EntityView::default(),
            attacker: EntityView::default(),
            parent: EntityView::default(),
        };
        s.player = EntityView {
            valid: 1,
            alive: 1,
            can_see: 1,
            extra: held,
            x: player.x,
            y: player.y,
            z: player.z,
            bb_y0: player.bb.y0,
            bb_y1: player.bb.y1,
            head: 1.62,
            width: 0.6,
        };
        s
    }
}

/// Fires stroll shaped path queries at the finder and reports how many come
/// back, which is the difference between a mob that wanders and one that stands.
fn probe(g: &Grid) {
    use deepslate_gen::pathfinder::{PathOut, PathQuery, MAX_PATH};
    let mut rng = deepslate_gen::random::Random::new(12345);
    let (mut ok, mut empty) = (0, 0);
    let mut total_len = 0;
    let (x, y, z) = (34.5f32, 65.0f32, 34.5f32);
    for _ in 0..500 {
        let tx = x.floor() as i32 + rng.next_int_bound(13) - 6;
        let ty = y.floor() as i32 + rng.next_int_bound(7) - 3;
        let tz = z.floor() as i32 + rng.next_int_bound(13) - 6;
        let q = PathQuery {
            bb_x0: x - 0.45,
            bb_y0: y,
            bb_z0: z - 0.45,
            x,
            z,
            bb_width: 0.9,
            bb_height: 0.9,
            tx: tx as f32 + 0.5,
            ty: ty as f32 + 0.5,
            tz: tz as f32 + 0.5,
            max_dist: 16.0,
            in_water: 0,
            avoid_water: 0,
        };
        let mut out = PathOut { len: 0, pts: [[0; 3]; MAX_PATH] };
        if deepslate_gen::pathfinder::shared_find(g, &q, &mut out) && out.len > 0 {
            ok += 1;
            total_len += out.len;
        } else {
            empty += 1;
        }
    }
    println!(
        "path probe: {ok}/500 found (avg {} nodes), {empty} empty",
        if ok > 0 { total_len / ok } else { 0 }
    );
}

fn main() {
    deepslate_gen::pathfinder::shared_init(flags());
    let g = build();
    probe(&g);

    let player = Body::new(50.5, 65.0, 50.5, 0.6, 1.8);
    let held = 0; // empty hand, so tempt never fires

    let mut actors = vec![
        Actor::new(mob::KIND_PIG, "pig", 34.5, 34.5, 65.0, 0.9, 0.9, 296),
        Actor::new(mob::KIND_COW, "cow", 36.5, 40.5, 65.0, 0.9, 1.3, 296),
        Actor::new(mob::KIND_SHEEP, "sheep", 32.5, 44.5, 65.0, 0.9, 1.3, 296),
        Actor::new(mob::KIND_SHEEP, "sheep2", 38.5, 36.5, 65.0, 0.9, 1.3, 296),
        Actor::new(mob::KIND_CHICKEN, "chicken", 34.5, 38.5, 65.0, 0.3, 0.4, 295),
        // Right against the two block wall at x=45.
        Actor::new(mob::KIND_PIG, "pig-wall", 43.5, 50.5, 65.0, 0.9, 0.9, 296),
        // On the shore, one step from open water.
        Actor::new(mob::KIND_COW, "cow-pond", 28.5, 28.5, 65.0, 0.9, 1.3, 296),
        // Beside the one block rise at x=60, which needs the footSize step up.
        Actor::new(mob::KIND_SHEEP, "sheep-step", 58.5, 20.5, 65.0, 0.9, 1.3, 296),
        Actor::new(mob::KIND_ZOMBIE, "zombie", 60.5, 60.5, 66.0, 0.6, 1.8, 0),
        // Out of the player's reach, so nothing outranks the run for shade.
        Actor::new(mob::KIND_SKELETON, "skeleton", 74.5, 74.5, 66.0, 0.6, 1.8, 0),
    ];

    // Settle everyone onto the ground before the AI gets a say.
    for a in &mut actors {
        for _ in 0..40 {
            a.body.yd -= 0.08;
            let (xd, yd, zd) = (a.body.xd, a.body.yd, a.body.zd);
            a.body.do_move(&g, xd, yd, zd);
        }
    }

    let ticks: i32 = std::env::args()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(6000);

    let mut world = g;
    let mut buf = [0u8; 96];
    let verbose = std::env::args().any(|a| a == "-v");
    // Which mob -v follows. Any name from the table above works.
    let watch = std::env::args()
        .nth(2)
        .unwrap_or_else(|| "pig".to_string());
    let watch = watch.as_str();

    for tick in 0..ticks {
        for a in &mut actors {
            a.no_action_time += 1;
            // checkDespawn resets it within 32 blocks, and unconditionally for
            // anything that cannot despawn at all. Animals are the latter.
            let dx = a.body.x - player.x;
            let dz = a.body.z - player.z;
            let dy = a.body.y - player.y;
            if a.kind <= mob::KIND_SHEEP || dx * dx + dy * dy + dz * dz < 32.0 * 32.0 {
                a.no_action_time = 0;
            }
            if a.flee_time > 0 {
                a.flee_time -= 1;
            }
            if a.fire > 0 {
                a.fire -= 1;
                a.fire_ticks += 1;
            }

            let s = a.snapshot(&world, &player, held);
            let mut out = MobOut::default();
            a.ai.tick(&s, &world, &mut out);

            let n = a.ai.describe(&mut buf);
            let names = String::from_utf8_lossy(&buf[..n]).to_string();
            *a.goal_hist.entry(names.clone()).or_insert(0) += 1;
            if verbose && a.name == watch && tick % 20 == 0 {
                let (i, l) = a.ai.nav_progress();
                println!(
                    "t{:<4} [{}] yya={:.2} jump={} body={:6.1} head={:6.1} nav={}/{} pos=({:.2},{:.2},{:.2}) gnd={} na={} wet={}",
                    tick, names.trim(), out.yya, out.jumping, out.y_rot, out.y_head_rot, i, l,
                    a.body.x, a.body.y, a.body.z, a.body.on_ground as i32, a.no_action_time,
                    a.body.in_water(&world) as i32
                );
            }

            if out.ignite != 0 {
                a.fire = a.fire.max(out.ignite * 20);
            }

            if out.eat_block != 0 {
                let (bx, by, bz) =
                    (a.body.x.floor() as i32, a.body.y.floor() as i32, a.body.z.floor() as i32);
                if out.eat_block == 2 && world.get(bx, by, bz) == blocks::TALLGRASS {
                    world.set(bx, by, bz, AIR);
                    a.eats += 1;
                    a.tall_eats += 1;
                } else if out.eat_block == 1 && world.get(bx, by - 1, bz) == blocks::GRASS {
                    world.set(bx, by - 1, bz, blocks::DIRT);
                    a.eats += 1;
                }
            }

            if (out.y_head_rot - a.body.y_rot).abs() > 0.05 {
                a.turn_ticks += 1;
            }
            a.body.y_rot = out.y_head_rot;
            a.body.y_body_rot = out.y_rot;
            a.body.x_rot = out.x_rot;
            // Mob::tick keeps the head within 75 degrees of the body and drags
            // the body round when it goes past.
            let mut hd = a.body.y_rot - a.body.y_body_rot;
            while hd < -180.0 { hd += 360.0 }
            while hd >= 180.0 { hd -= 360.0 }
            a.head_off = a.head_off.max(hd.abs());
            if hd.abs() > 1.0 { a.head_turn_ticks += 1 }

            let (px, pz) = (a.body.x, a.body.z);
            let in_water = a.body.in_water(&world);
            if out.jumping != 0 {
                a.jump_ticks += 1;
                if in_water {
                    a.body.yd += 0.04;
                } else if a.body.on_ground {
                    a.body.yd = 0.42;
                }
            }
            let (xxa, yya) = (out.xxa * 0.98, out.yya * 0.98);
            a.body.travel(&world, xxa, yya);

            if in_water {
                a.wet_ticks += 1;
            }
            if a.body.horiz {
                a.collide_ticks += 1;
            }
            let (mx, mz) = (a.body.x - px, a.body.z - pz);
            a.dist += (mx * mx + mz * mz).sqrt();
            a.cells.insert((a.body.x.floor() as i32, a.body.z.floor() as i32));
            a.ymin = a.ymin.min(a.body.y);
            a.ymax = a.ymax.max(a.body.y);
        }
    }

    println!("\n{ticks} ticks ({:.0}s of game time)\n", ticks as f32 / 20.0);
    println!(
        "{:<10} {:>7} {:>7} {:>6} {:>6} {:>6} {:>5} {:>5} {:>6} {:>6} {:>4}  {}",
        "mob", "blocks", "cells", "jump%", "hit%", "wet%", "eats", "tall", "fire%", "head%", "dy", "top goals"
    );
    for a in &actors {
        let pc = |n: i32| 100.0 * n as f32 / ticks as f32;
        let mut hist: Vec<_> = a.goal_hist.iter().collect();
        hist.sort_by_key(|(_, n)| -**n);
        let top: Vec<String> = hist
            .iter()
            .take(3)
            .map(|(k, n)| format!("{}:{}%", k.trim().trim_end_matches(','), (100 * **n) / ticks))
            .collect();
        println!(
            "{:<10} {:>7.0} {:>7} {:>5.1}% {:>5.1}% {:>5.1}% {:>5} {:>5} {:>5.1}% {:>5.1}% {:>4.0}  {}",
            a.name,
            a.dist,
            a.cells.len(),
            pc(a.jump_ticks),
            pc(a.collide_ticks),
            pc(a.wet_ticks),
            a.eats,
            a.tall_eats,
            pc(a.fire_ticks),
            pc(a.head_turn_ticks),
            a.ymax - a.ymin,
            top.join(" ")
        );
    }
    println!();
}

fn flags() -> [u8; 256] {
    let mut f = [0u8; 256];
    for id in 1..256usize {
        f[id] = F_SOLID;
    }
    f[0] = 0;
    f[blocks::WATER as usize] = F_WATER;
    f[blocks::CALM_WATER as usize] = F_WATER;
    // isSolidPhys is false for a bush, so the path walks straight through it.
    f[blocks::TALLGRASS as usize] = 0;
    f
}
