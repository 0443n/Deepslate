//! Mob spawning, ported from src/world/level/mob_spawner.cpp.
//!
//! The decisions are all here. C++ keeps entity construction, because whether a
//! mob may stand where it was offered is a per mob virtual reading state the
//! host never sends over, see Monster::isDarkEnoughToSpawn.

use crate::random::Random;

pub const BASE_ENEMY: i32 = 1;
pub const BASE_CREATURE: i32 = 2;

pub const ID_CHICKEN: i32 = 10;
pub const ID_COW: i32 = 11;
pub const ID_PIG: i32 = 12;
pub const ID_SHEEP: i32 = 13;
pub const ID_ZOMBIE: i32 = 32;
pub const ID_CREEPER: i32 = 33;
pub const ID_SKELETON: i32 = 34;
pub const ID_SPIDER: i32 = 35;

/// One row of a spawn table, matching the C++ SpawnEntry.
#[derive(Clone, Copy)]
pub struct SpawnEntry {
    pub mob_id: i32,
    pub weight: i32,
    pub min_count: i32,
    pub max_count: i32,
}

const fn e(mob_id: i32, weight: i32, min_count: i32, max_count: i32) -> SpawnEntry {
    SpawnEntry { mob_id, weight, min_count, max_count }
}

const CREATURE_TABLE: [SpawnEntry; 4] = [
    e(ID_SHEEP, 12, 2, 3),
    e(ID_PIG, 10, 1, 3),
    e(ID_CHICKEN, 10, 2, 4),
    e(ID_COW, 8, 2, 3),
];
const CREATURE_TOTAL_WEIGHT: i32 = 40;

const MONSTER_TABLE: [SpawnEntry; 4] = [
    e(ID_ZOMBIE, 12, 2, 4),
    e(ID_SPIDER, 8, 2, 3),
    e(ID_SKELETON, 6, 1, 3),
    e(ID_CREEPER, 4, 1, 1),
];
const MONSTER_TOTAL_WEIGHT: i32 = 30;

const CREATURE_MAX_PER_LEVEL: i32 = 15;
const MONSTER_MAX_PER_LEVEL: i32 = 20;

const MIN_SPAWN_DISTANCE: i32 = 24;
/// The same window the monsters use, so the cap means the same for both.
const CREATURE_RADIUS: i32 = 96;
const CREATURE_LIGHT: i32 = 9;
const MAX_SPAWN_CLUSTER: i32 = 4;
const SPAWN_ATTEMPTS: i32 = 8;
/// Vanilla rolls this per mob as it spawns, so a herd is adults with the odd calf.
const BABY_ODDS: i32 = 20;
const SURFACE_PROBE_ODDS: i32 = 4;
const PROBE_SNAP: i32 = 8;
/// Animals never despawn, so worldgen has to leave the runtime spawner headroom
/// under the cap, or it hands over a world where nothing can ever spawn again.
const GEN_CREATURE_CAP: i32 = 24;
const CREATURE_PROBABILITY: f32 = 0.08;

pub const WORLD_H: i32 = 128;
pub const AIR: i32 = 0;
pub const GRASS: i32 = 2;

/// What the spawner asks of the game. One implementation forwards to C++, the
/// tests use a plain grid.
pub trait SpawnHost {
    fn tile(&self, x: i32, y: i32, z: i32) -> i32;
    fn solid_blocking(&self, x: i32, y: i32, z: i32) -> bool;
    fn brightness(&self, x: i32, y: i32, z: i32) -> i32;
    fn top_solid(&self, x: i32, z: i32) -> i32;
    fn chunk_ready(&self, cx: i32, cz: i32) -> bool;
    fn count_base(&self, base: i32) -> i32;
    fn count_type(&self, mob_id: i32) -> i32;
    fn count_creatures_near(&self, px: f32, pz: f32, r: f32) -> i32;
    /// The player's feet, which is y minus heightOffset on the C++ side.
    fn player(&self) -> Option<(f32, f32, f32)>;
    fn player_within(&self, x: f32, y: f32, z: f32, r: f32) -> bool;
    fn peaceful(&self) -> bool;
    /// Resident chunk slots, walked by index for the worldgen pass.
    fn slot_count(&self) -> i32;
    fn slot_chunk(&self, i: i32) -> Option<(i32, i32)>;
    /// True when the mob was actually placed. C++ may still refuse it.
    fn place(&mut self, mob_id: i32, x: f32, y: f32, z: f32, y_rot: f32, baby: bool) -> bool;
}

fn water_or_lava(id: i32) -> bool {
    crate::pathfinder::shared_flag(id, crate::pathfinder::F_WATER)
        || crate::pathfinder::shared_flag(id, crate::pathfinder::F_LAVA)
}

fn pick_weighted(table: &[SpawnEntry], total_weight: i32, rng: &mut Random) -> SpawnEntry {
    let mut r = rng.next_int_bound(total_weight);
    for entry in table {
        r -= entry.weight;
        if r < 0 {
            return *entry;
        }
    }
    table[table.len() - 1]
}

fn spawn_ok(h: &dyn SpawnHost, x: i32, y: i32, z: i32) -> bool {
    if y <= 0 || y + 1 >= WORLD_H {
        return false;
    }
    if !h.solid_blocking(x, y - 1, z) {
        return false;
    }
    if h.solid_blocking(x, y, z) || h.solid_blocking(x, y + 1, z) {
        return false;
    }
    !water_or_lava(h.tile(x, y, z))
}

/// Vanilla puts animals on lit grass. Nothing else keeps them off treetops and
/// bare stone, which is where a plain surface probe most often points.
fn creature_spawn_ok(h: &dyn SpawnHost, x: i32, y: i32, z: i32) -> bool {
    spawn_ok(h, x, y, z)
        && h.tile(x, y - 1, z) == GRASS
        && h.brightness(x, y, z) >= CREATURE_LIGHT
}

/// A spread of up to five blocks either way, the shape vanilla uses to scatter a
/// herd around the block the attempt landed on.
fn scatter(rng: &mut Random, spread: i32) -> i32 {
    rng.next_int_bound(spread) - rng.next_int_bound(spread)
}

pub struct Spawner {
    rng: Random,
}

impl Spawner {
    pub fn new(seed: i32) -> Spawner {
        Spawner { rng: Random::new(seed) }
    }

    pub fn set_seed(&mut self, seed: i32) {
        self.rng.set_seed(seed);
    }

    pub fn tick(&mut self, h: &mut dyn SpawnHost, enemies: bool, friendlies: bool) {
        if friendlies {
            self.spawn_creatures(h);
        }
        if enemies {
            self.spawn_monsters(h);
        }
    }

    fn spawn_creatures(&mut self, h: &mut dyn SpawnHost) {
        let (px, pfy, pz) = match h.player() {
            Some(p) => p,
            None => return,
        };
        let r = CREATURE_RADIUS / 16;
        let pcx = crate::mth::floor(px / 16.0);
        let pcz = crate::mth::floor(pz / 16.0);

        let mut count = h.count_creatures_near(px, pz, CREATURE_RADIUS as f32);
        for _ in 0..SPAWN_ATTEMPTS {
            if count > CREATURE_MAX_PER_LEVEL {
                return;
            }
            let cx = pcx + self.rng.next_int_bound(2 * r + 1) - r;
            let cz = pcz + self.rng.next_int_bound(2 * r + 1) - r;
            if !h.chunk_ready(cx, cz) {
                continue;
            }
            let bx = cx * 16 + self.rng.next_int_bound(16);
            let bz = cz * 16 + self.rng.next_int_bound(16);
            let by = h.top_solid(bx, bz);
            if !creature_spawn_ok(h, bx, by, bz) {
                continue;
            }
            let dx = bx as f32 + 0.5 - px;
            let dy = by as f32 - pfy;
            let dz = bz as f32 + 0.5 - pz;
            let min = (MIN_SPAWN_DISTANCE * MIN_SPAWN_DISTANCE) as f32;
            if dx * dx + dy * dy + dz * dz < min {
                continue;
            }

            let entry = pick_weighted(&CREATURE_TABLE, CREATURE_TOTAL_WEIGHT, &mut self.rng);
            let cluster =
                entry.min_count + self.rng.next_int_bound(1 + entry.max_count - entry.min_count);
            for _ in 0..cluster {
                let sx = bx + scatter(&mut self.rng, 6);
                let sz = bz + scatter(&mut self.rng, 6);
                let sy = h.top_solid(sx, sz);
                if !creature_spawn_ok(h, sx, sy, sz) {
                    continue;
                }
                let y_rot = self.rng.next_float() * 360.0;
                // Rolled before the offer rather than after, since whether the
                // mob was accepted is only known once C++ has answered.
                let baby = self.rng.next_int_bound(BABY_ODDS) == 0;
                if h.place(entry.mob_id, sx as f32 + 0.5, sy as f32, sz as f32 + 0.5, y_rot, baby) {
                    count += 1;
                }
            }
        }
    }

    /// Most attempts drop somewhere in the column, so caves fill as well as the
    /// surface. The rest go straight to the top block.
    fn probe_standable_y(&mut self, h: &dyn SpawnHost, x: i32, z: i32) -> i32 {
        if self.rng.next_int_bound(SURFACE_PROBE_ODDS) == 0 {
            let y = h.top_solid(x, z);
            return if spawn_ok(h, x, y, z) { y } else { -1 };
        }
        let y0 = self.rng.next_int_bound(WORLD_H);
        for d in 0..=PROBE_SNAP {
            if spawn_ok(h, x, y0 - d, z) {
                return y0 - d;
            }
            if d != 0 && spawn_ok(h, x, y0 + d, z) {
                return y0 + d;
            }
        }
        -1
    }

    fn spawn_monsters(&mut self, h: &mut dyn SpawnHost) {
        let (px, _, pz) = match h.player() {
            Some(p) => p,
            None => return,
        };
        if h.peaceful() {
            return;
        }
        let pcx = crate::mth::floor(px / 16.0);
        let pcz = crate::mth::floor(pz / 16.0);
        let r = 96 / 16;

        let mut count = h.count_base(BASE_ENEMY);
        if count > MONSTER_MAX_PER_LEVEL {
            return;
        }

        for _ in 0..SPAWN_ATTEMPTS {
            if count > MONSTER_MAX_PER_LEVEL {
                return;
            }
            let cx = pcx + self.rng.next_int_bound(2 * r + 1) - r;
            let cz = pcz + self.rng.next_int_bound(2 * r + 1) - r;
            if !h.chunk_ready(cx, cz) {
                continue;
            }
            let x_start = cx * 16 + self.rng.next_int_bound(16);
            let z_start = cz * 16 + self.rng.next_int_bound(16);
            let y_start = self.probe_standable_y(h, x_start, z_start);
            if y_start < 0 {
                continue;
            }
            if h.solid_blocking(x_start, y_start, z_start) {
                continue;
            }
            if h.tile(x_start, y_start, z_start) != AIR {
                continue;
            }

            let mut cluster = 0;
            for _ in 0..3 {
                if cluster >= MAX_SPAWN_CLUSTER {
                    break;
                }
                let (mut x, y, mut z) = (x_start, y_start, z_start);
                let mut kind: Option<SpawnEntry> = None;
                let mut pack_max = 0;
                let mut pack_count = 0;

                for _ in 0..4 {
                    if kind.is_some() && pack_count > pack_max {
                        break;
                    }
                    x += scatter(&mut self.rng, 6);
                    z += scatter(&mut self.rng, 6);
                    if !spawn_ok(h, x, y, z) {
                        continue;
                    }
                    let (xx, yy, zz) = (x as f32 + 0.5, y as f32, z as f32 + 0.5);
                    if h.player_within(xx, yy, zz, MIN_SPAWN_DISTANCE as f32) {
                        continue;
                    }
                    if kind.is_none() {
                        let entry =
                            pick_weighted(&MONSTER_TABLE, MONSTER_TOTAL_WEIGHT, &mut self.rng);
                        let type_max = (1.5 * entry.weight as f32 * MONSTER_MAX_PER_LEVEL as f32)
                            as i32
                            / MONSTER_TOTAL_WEIGHT;
                        if h.count_type(entry.mob_id) >= type_max {
                            break;
                        }
                        pack_max = entry.min_count
                            + self.rng.next_int_bound(1 + entry.max_count - entry.min_count);
                        kind = Some(entry);
                    }
                    let entry = match kind {
                        Some(k) => k,
                        None => break,
                    };
                    let y_rot = self.rng.next_float() * 360.0;
                    if !h.place(entry.mob_id, xx, yy, zz, y_rot, false) {
                        continue;
                    }
                    pack_count += 1;
                    count += 1;
                    cluster += 1;
                    if cluster >= MAX_SPAWN_CLUSTER {
                        break;
                    }
                }
            }
        }
    }

    /// The worldgen pass, walked over the resident chunks in a shuffled order so
    /// the herds are not biased toward whichever slot happens to come first.
    pub fn populate_initial(&mut self, h: &mut dyn SpawnHost) {
        let n = h.slot_count();
        if n <= 0 || n as usize > ORDER_MAX {
            return;
        }
        let mut order = [0u8; ORDER_MAX];
        for i in 0..n {
            order[i as usize] = i as u8;
        }
        for i in (1..n).rev() {
            let j = self.rng.next_int_bound(i + 1);
            order.swap(i as usize, j as usize);
        }

        for oi in 0..n {
            let (cx, cz) = match h.slot_chunk(order[oi as usize] as i32) {
                Some(c) => c,
                None => continue,
            };
            let (xo, zo) = (cx * 16, cz * 16);
            while self.rng.next_float() < CREATURE_PROBABILITY {
                if h.count_base(BASE_CREATURE) >= GEN_CREATURE_CAP {
                    return;
                }
                let entry = pick_weighted(&CREATURE_TABLE, CREATURE_TOTAL_WEIGHT, &mut self.rng);
                let count = entry.min_count
                    + self.rng.next_int_bound(1 + entry.max_count - entry.min_count);
                let mut x = xo + self.rng.next_int_bound(16);
                let mut z = zo + self.rng.next_int_bound(16);
                let (start_x, start_z) = (x, z);
                for _ in 0..count {
                    for _ in 0..4 {
                        let y = h.top_solid(x, z);
                        if spawn_ok(h, x, y, z) {
                            let y_rot = self.rng.next_float() * 360.0;
                            let baby = self.rng.next_int_bound(BABY_ODDS) == 0;
                            h.place(
                                entry.mob_id,
                                x as f32 + 0.5,
                                y as f32,
                                z as f32 + 0.5,
                                y_rot,
                                baby,
                            );
                            break;
                        }
                        x += scatter(&mut self.rng, 5);
                        z += scatter(&mut self.rng, 5);
                        // The herd stays inside the chunk that invited it.
                        while x < xo || x >= xo + 16 || z < zo || z >= zo + 16 {
                            x = start_x + scatter(&mut self.rng, 5);
                            z = start_z + scatter(&mut self.rng, 5);
                        }
                    }
                }
            }
        }
    }
}

/// Matches the C++ assert that the slot square fits a byte of shuffle order.
const ORDER_MAX: usize = 256;
