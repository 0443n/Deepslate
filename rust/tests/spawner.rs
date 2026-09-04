//! Host cover for rust/src/spawner.rs.
//!
//! The C++ this replaced read Level and the entity list directly, so it could
//! never be built by tools/gen-vectors.sh the way the generator is. These
//! checks stand in for that, over a grid world with a known shape.

use deepslate_gen::pathfinder::{F_SOLID, F_WATER};
use deepslate_gen::spawner::{SpawnHost, Spawner, BASE_CREATURE, BASE_ENEMY, GRASS};

const SX: i32 = 128;
const SZ: i32 = 128;
const SY: i32 = 128;
const GROUND: i32 = 64;

const STONE: u8 = 1;
const DIRT: u8 = 3;
const WATER: u8 = 8;

struct Placed {
    mob_id: i32,
    x: f32,
    y: f32,
    z: f32,
    baby: bool,
}

struct Grid {
    b: Vec<u8>,
    /// Light everywhere unless a column was darkened on purpose.
    dark: Vec<bool>,
    player: Option<(f32, f32, f32)>,
    peaceful: bool,
    placed: Vec<Placed>,
    /// Set when the host should refuse everything, standing in for a canSpawn
    /// that says no.
    refuse: bool,
}

impl Grid {
    fn new() -> Grid {
        let mut g = Grid {
            b: vec![0u8; (SX * SY * SZ) as usize],
            dark: vec![false; (SX * SZ) as usize],
            player: Some((64.5, GROUND as f32 + 1.0, 64.5)),
            peaceful: false,
            placed: Vec::new(),
            refuse: false,
        };
        for x in 0..SX {
            for z in 0..SZ {
                for y in 1..GROUND {
                    g.set(x, y, z, STONE);
                }
                g.set(x, GROUND, z, DIRT);
                g.set(x, GROUND + 1, z, GRASS as u8);
            }
        }
        g
    }

    fn idx(x: i32, y: i32, z: i32) -> Option<usize> {
        if x < 0 || y < 0 || z < 0 || x >= SX || y >= SY || z >= SZ {
            return None;
        }
        Some(((y * SZ + z) * SX + x) as usize)
    }

    fn get(&self, x: i32, y: i32, z: i32) -> u8 {
        Grid::idx(x, y, z).map_or(0, |i| self.b[i])
    }

    fn set(&mut self, x: i32, y: i32, z: i32, id: u8) {
        if let Some(i) = Grid::idx(x, y, z) {
            self.b[i] = id;
        }
    }

    fn darken(&mut self, x: i32, z: i32) {
        self.dark[(z * SX + x) as usize] = true;
    }
}

impl SpawnHost for Grid {
    fn tile(&self, x: i32, y: i32, z: i32) -> i32 {
        self.get(x, y, z) as i32
    }
    fn solid_blocking(&self, x: i32, y: i32, z: i32) -> bool {
        let id = self.get(x, y, z);
        id != 0 && id != WATER
    }
    fn brightness(&self, x: i32, _y: i32, z: i32) -> i32 {
        if x < 0 || z < 0 || x >= SX || z >= SZ {
            return 0;
        }
        if self.dark[(z * SX + x) as usize] {
            0
        } else {
            15
        }
    }
    fn top_solid(&self, x: i32, z: i32) -> i32 {
        for y in (0..SY).rev() {
            if self.solid_blocking(x, y, z) {
                return y + 1;
            }
        }
        0
    }
    fn chunk_ready(&self, cx: i32, cz: i32) -> bool {
        cx >= 0 && cz >= 0 && cx < SX / 16 && cz < SZ / 16
    }
    fn count_base(&self, base: i32) -> i32 {
        self.placed
            .iter()
            .filter(|p| {
                let b = if p.mob_id < 20 { BASE_CREATURE } else { BASE_ENEMY };
                b == base
            })
            .count() as i32
    }
    fn count_type(&self, mob_id: i32) -> i32 {
        self.placed.iter().filter(|p| p.mob_id == mob_id).count() as i32
    }
    fn count_creatures_near(&self, px: f32, pz: f32, r: f32) -> i32 {
        self.placed
            .iter()
            .filter(|p| p.mob_id < 20)
            .filter(|p| {
                let (dx, dz) = (p.x - px, p.z - pz);
                dx * dx + dz * dz <= r * r
            })
            .count() as i32
    }
    fn player(&self) -> Option<(f32, f32, f32)> {
        self.player
    }
    fn player_within(&self, x: f32, y: f32, z: f32, r: f32) -> bool {
        match self.player {
            Some((px, py, pz)) => {
                let (dx, dy, dz) = (px - x, py - y, pz - z);
                dx * dx + dy * dy + dz * dz < r * r
            }
            None => false,
        }
    }
    fn peaceful(&self) -> bool {
        self.peaceful
    }
    fn slot_count(&self) -> i32 {
        (SX / 16) * (SZ / 16)
    }
    fn slot_chunk(&self, i: i32) -> Option<(i32, i32)> {
        if i < 0 || i >= self.slot_count() {
            return None;
        }
        Some((i % (SX / 16), i / (SX / 16)))
    }
    fn place(&mut self, mob_id: i32, x: f32, y: f32, z: f32, _y_rot: f32, baby: bool) -> bool {
        if self.refuse {
            return false;
        }
        self.placed.push(Placed { mob_id, x, y, z, baby });
        true
    }
}

fn flags() -> [u8; 256] {
    let mut f = [0u8; 256];
    for id in 1..256usize {
        f[id] = F_SOLID;
    }
    f[0] = 0;
    f[WATER as usize] = F_WATER;
    f
}

fn init() {
    deepslate_gen::pathfinder::shared_init(flags());
}

#[test]
fn creatures_land_on_lit_grass_away_from_the_player() {
    init();
    let mut g = Grid::new();
    let mut s = Spawner::new(12345);
    for _ in 0..400 {
        s.tick(&mut g, false, true);
    }
    assert!(!g.placed.is_empty(), "no animals spawned in 400 passes");

    let (px, _, pz) = g.player.unwrap();
    for p in &g.placed {
        assert!(p.mob_id >= 10 && p.mob_id <= 13, "kind {} is not an animal", p.mob_id);
        let (bx, by, bz) = (p.x.floor() as i32, p.y.floor() as i32, p.z.floor() as i32);
        assert_eq!(
            g.get(bx, by - 1, bz),
            GRASS as u8,
            "animal at {bx},{by},{bz} is not standing on grass"
        );
        let (dx, dz) = (p.x - px, p.z - pz);
        assert!(
            dx * dx + dz * dz >= 20.0 * 20.0,
            "animal at {},{} is on top of the player",
            p.x,
            p.z
        );
    }
}

#[test]
fn creature_cap_holds() {
    init();
    let mut g = Grid::new();
    let mut s = Spawner::new(999);
    for _ in 0..2000 {
        s.tick(&mut g, false, true);
    }
    // The cap is checked per attempt and a cluster may overshoot it, so this
    // guards the runaway case rather than the exact ceiling.
    let n = g.count_base(BASE_CREATURE);
    assert!(n > 0 && n < 40, "creature count ran to {n}");
}

#[test]
fn darkness_keeps_animals_out_and_lets_monsters_in() {
    init();
    let mut g = Grid::new();
    for x in 0..SX {
        for z in 0..SZ {
            g.darken(x, z);
        }
    }
    let mut s = Spawner::new(4242);
    for _ in 0..400 {
        s.tick(&mut g, true, true);
    }
    assert_eq!(g.count_base(BASE_CREATURE), 0, "animals spawned in the dark");
    assert!(g.count_base(BASE_ENEMY) > 0, "no monsters spawned in the dark");
}

#[test]
fn peaceful_stops_monsters() {
    init();
    let mut g = Grid::new();
    g.peaceful = true;
    let mut s = Spawner::new(7);
    for _ in 0..400 {
        s.tick(&mut g, true, false);
    }
    assert_eq!(g.count_base(BASE_ENEMY), 0, "monsters spawned on peaceful");
}

#[test]
fn a_refusing_host_never_fills_the_world() {
    init();
    let mut g = Grid::new();
    g.refuse = true;
    let mut s = Spawner::new(31337);
    for _ in 0..400 {
        s.tick(&mut g, true, true);
    }
    assert!(g.placed.is_empty(), "a refused offer was counted as a spawn");
}

#[test]
fn no_player_means_no_spawns() {
    init();
    let mut g = Grid::new();
    g.player = None;
    let mut s = Spawner::new(5);
    for _ in 0..400 {
        s.tick(&mut g, true, true);
    }
    assert!(g.placed.is_empty(), "spawned with no player in the level");
}

#[test]
fn worldgen_pass_is_seed_stable_and_capped() {
    init();
    let run = |seed: i32| {
        let mut g = Grid::new();
        let mut s = Spawner::new(seed);
        s.populate_initial(&mut g);
        g.placed.iter().map(|p| (p.mob_id, p.x, p.z, p.baby)).collect::<Vec<_>>()
    };
    let a = run(2026);
    let b = run(2026);
    assert_eq!(a, b, "the worldgen pass is not reproducible from its seed");
    assert!(!a.is_empty(), "the worldgen pass placed nothing");
    // GEN_CREATURE_CAP is 24, and a herd may finish past the check.
    assert!(a.len() < 32, "the worldgen pass placed {} animals", a.len());
    assert_ne!(a, run(2027), "two seeds produced the same herd layout");
}

#[test]
fn spawns_avoid_water() {
    init();
    let mut g = Grid::new();
    for x in 0..SX {
        for z in 0..SZ {
            if (x - 40) * (x - 40) + (z - 40) * (z - 40) <= 200 {
                g.set(x, GROUND + 1, z, WATER);
                g.set(x, GROUND, z, WATER);
            }
        }
    }
    let mut s = Spawner::new(808);
    for _ in 0..600 {
        s.tick(&mut g, true, true);
    }
    for p in &g.placed {
        let (bx, by, bz) = (p.x.floor() as i32, p.y.floor() as i32, p.z.floor() as i32);
        assert_ne!(g.get(bx, by, bz), WATER, "a mob spawned standing in water");
    }
}
