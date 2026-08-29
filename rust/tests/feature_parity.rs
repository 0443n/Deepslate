//! Replays tools/gen-feature-vectors.cpp against the Rust port and compares the
//! traces line for line. The world here is the same flat double the C++ ran
//! against, see tools/testworld/world/level/world.h, so any difference is in the
//! feature logic or in the order it draws from Random.

use deepslate_gen::blocks::{self, WORLD_H};
use deepslate_gen::features::{self, PendingList};
use deepslate_gen::random::Random;
use deepslate_gen::world::World;
use deepslate_gen::{biome_surface, classify_biome};

const WORLD_W: i32 = 256;
const WORLD_D: i32 = 256;

struct TestWorld {
    id: Vec<u8>,
    data: Vec<u8>,
    trace: Vec<String>,
}

fn idx(x: i32, y: i32, z: i32) -> usize {
    ((x * WORLD_H + y) * WORLD_D + z) as usize
}

impl TestWorld {
    fn new() -> Self {
        let n = (WORLD_W * WORLD_H * WORLD_D) as usize;
        Self {
            id: vec![blocks::AIR; n],
            data: vec![0; n],
            trace: Vec::new(),
        }
    }

    fn put(&mut self, x: i32, y: i32, z: i32, id: u8) {
        self.id[idx(x, y, z)] = id;
    }
}

impl World for TestWorld {
    fn block(&self, x: i32, y: i32, z: i32) -> u8 {
        if y < 0 || y >= WORLD_H {
            return blocks::AIR;
        }
        if !self.ready(x, z) {
            return blocks::INVISIBLE_BEDROCK;
        }
        self.id[idx(x, y, z)]
    }

    fn ready(&self, x: i32, z: i32) -> bool {
        x >= 0 && x < WORLD_W && z >= 0 && z < WORLD_D
    }

    fn set_block_and_data(&mut self, x: i32, y: i32, z: i32, id: u8, data: u8) {
        if y < 0 || y >= WORLD_H || !self.ready(x, z) {
            return;
        }
        self.id[idx(x, y, z)] = id;
        self.data[idx(x, y, z)] = data;
        self.trace.push(format!("S {} {} {} {} {}", x, y, z, id, data));
    }

    fn schedule_tick(&mut self, x: i32, y: i32, z: i32, id: u8, delay: i32) {
        self.trace.push(format!("T {} {} {} {} {}", x, y, z, id, delay));
    }

    fn light_raw(&self, x: i32, y: i32, z: i32) -> i32 {
        if self.can_see_sky(x, y, z) {
            15
        } else {
            0
        }
    }

    fn can_see_sky(&self, x: i32, y: i32, z: i32) -> bool {
        if y < 0 || !self.ready(x, z) {
            return false;
        }
        for yy in y + 1..WORLD_H {
            if self.id[idx(x, yy, z)] != blocks::AIR {
                return false;
            }
        }
        true
    }
}

fn build_terrain(w: &mut TestWorld) {
    w.id.iter_mut().for_each(|b| *b = blocks::AIR);
    w.data.iter_mut().for_each(|b| *b = 0);

    const SEA: i32 = 63;
    for x in 0..WORLD_W {
        for z in 0..WORLD_D {
            let mut h = 58 + (x * 7 + z * 13) % 11;
            let basin = ((x >> 4) + (z >> 4)) % 5 == 0;
            let desert = x >= WORLD_W / 2 && !basin;
            if basin {
                h -= 6;
            }

            w.put(x, 0, z, blocks::BEDROCK);
            for y in 1..=h - 4 {
                w.put(x, y, z, blocks::STONE);
            }
            for y in h - 3..h {
                w.put(x, y, z, if desert { blocks::SAND } else { blocks::DIRT });
            }
            w.put(
                x,
                h,
                z,
                if basin || desert {
                    blocks::SAND
                } else {
                    blocks::GRASS
                },
            );
            if basin {
                for y in h + 1..=SEA {
                    w.put(x, y, z, blocks::CALM_WATER);
                }
            }

            if (x / 8 + z / 8) % 3 == 0 {
                for y in 30..=33 {
                    w.put(x, y, z, blocks::AIR);
                }
            }
        }
    }
}

fn surface_at(w: &TestWorld, x: i32, z: i32) -> i32 {
    features::heightmap_at(w, x, z)
}

#[test]
fn matches_cpp_vectors() {
    let mut w = TestWorld::new();
    let seed = 20260829;

    macro_rules! feature {
        ($name:expr) => {{
            w.trace.push(format!("F {}", $name));
            build_terrain(&mut w);
        }};
    }
    macro_rules! checkpoint {
        ($r:expr) => {{
            let v = $r.next_int_bound(0x4000_0000);
            w.trace.push(format!("R {}", v));
        }};
    }

    feature!("classify_biome");
    for i in 0..64 {
        for j in 0..64 {
            let t = i as f32 / 63.0;
            let d = j as f32 / 63.0;
            let b = classify_biome(t, d);
            let (top, mat) = biome_surface(b);
            w.trace.push(format!("B {} {} {}", b as i32, top, mat));
        }
    }

    feature!("tree_oak");
    let mut r = Random::new(seed + 1);
    for i in 0..40 {
        let (x, z) = (20 + i * 5, 30 + i * 3);
        let y = surface_at(&w, x, z);
        features::tree_oak(&mut w, &mut r, x, y, z);
    }
    checkpoint!(r);

    feature!("tree_birch");
    let mut r = Random::new(seed + 2);
    for i in 0..40 {
        let (x, z) = (22 + i * 5, 40 + i * 3);
        let y = surface_at(&w, x, z);
        features::tree_birch(&mut w, &mut r, x, y, z);
    }
    checkpoint!(r);

    feature!("tree_spruce");
    let mut r = Random::new(seed + 3);
    for i in 0..40 {
        let (x, z) = (24 + i * 5, 50 + i * 3);
        let y = surface_at(&w, x, z);
        features::tree_spruce(&mut w, &mut r, x, y, z);
    }
    checkpoint!(r);

    feature!("tree_pine");
    let mut r = Random::new(seed + 4);
    for i in 0..40 {
        let (x, z) = (26 + i * 5, 60 + i * 3);
        let y = surface_at(&w, x, z);
        features::tree_pine(&mut w, &mut r, x, y, z);
    }
    checkpoint!(r);

    feature!("cactus");
    let mut r = Random::new(seed + 5);
    for i in 0..500 {
        let (x, z) = (WORLD_W / 2 + (i * 13) % 120, (i * 7) % 200);
        let y = surface_at(&w, x, z);
        features::cactus_feature(&mut w, &mut r, x, y, z);
    }
    checkpoint!(r);

    feature!("reeds");
    let mut r = Random::new(seed + 6);
    for i in 0..900 {
        let (x, z) = (2 + (i * 3) % 200, 2 + (i * 11) % 200);
        let y = surface_at(&w, x, z);
        features::reeds_feature(&mut w, &mut r, x, y, z);
    }
    checkpoint!(r);

    feature!("ore");
    let mut r = Random::new(seed + 7);
    const ORE_TILES: [u8; 5] = [16, 15, 14, blocks::GRAVEL, blocks::DIRT];
    for i in 0..50 {
        features::ore_feature(
            &mut w,
            &mut r,
            (i * 11) % 200,
            8 + (i * 3) % 40,
            (i * 17) % 200,
            ORE_TILES[(i % 5) as usize],
            4 + (i % 6) * 6,
        );
    }
    checkpoint!(r);

    feature!("clay");
    let mut r = Random::new(seed + 8);
    for i in 0..200 {
        features::clay_feature(&mut w, &mut r, (i * 13) % 200, 63, (i * 7) % 200);
    }
    checkpoint!(r);

    feature!("spring");
    build_terrain(&mut w);
    for i in 0..400 {
        let (x, y, z) = (3 + (i * 29) % 200, 20 + (i * 11) % 25, 3 + (i * 41) % 200);
        w.put(x, y, z, blocks::AIR);
        w.put(x + 1, y, z, blocks::AIR);
    }
    for i in 0..400 {
        let (x, y, z) = (3 + (i * 29) % 200, 20 + (i * 11) % 25, 3 + (i * 41) % 200);
        let tile = if i & 1 != 0 {
            blocks::WATER
        } else {
            blocks::LAVA
        };
        features::spring_feature(&mut w, x, y, z, tile);
    }

    feature!("lake");
    let mut r = Random::new(seed + 9);
    for i in 0..30 {
        let (x, z) = (20 + (i * 23) % 180, 20 + (i * 31) % 180);
        let tile = if i & 1 != 0 {
            blocks::WATER
        } else {
            blocks::LAVA
        };
        features::lake_feature(&mut w, &mut r, x, 70, z, tile);
    }
    checkpoint!(r);

    feature!("snow");
    let mut m_temp = [0.0f32; 16 * 16];
    for i in 0..16 * 16 {
        m_temp[i] = (i % 32) as f32 / 40.0;
    }
    for cx in 0..8 {
        for cz in 0..8 {
            features::snow_cap(&mut w, cx, cz, &m_temp);
        }
    }

    feature!("flower");
    let mut r = Random::new(seed + 10);
    let mut pending = PendingList::new();
    for i in 0..50 {
        let (x, z) = (15 + (i * 19) % 180, 15 + (i * 29) % 180);
        let y = surface_at(&w, x, z);
        let tile = if i & 1 != 0 {
            blocks::FLOWER
        } else {
            blocks::ROSE
        };
        features::flower_feature(&w, &mut r, &mut pending, x, y, z, tile);
    }
    w.trace.push("P place".to_string());
    features::world_place_flowers(&mut w, &mut pending);
    checkpoint!(r);

    feature!("mushroom");
    let mut r = Random::new(seed + 11);
    let mut pending = PendingList::new();
    for i in 0..120 {
        let (x, z) = (17 + (i * 23) % 180, 17 + (i * 37) % 180);
        let tile = if i & 1 != 0 {
            blocks::MUSHROOM_BROWN
        } else {
            blocks::MUSHROOM_RED
        };
        features::mushroom_feature(&w, &mut r, &mut pending, x, 32, z, tile);
    }
    w.trace.push("P place".to_string());
    features::world_place_mushrooms(&mut w, &mut pending);
    checkpoint!(r);

    feature!("caves");
    for cx in 2..8 {
        for cz in 2..8 {
            deepslate_gen::caves::cave_feature(&mut w, 20260829, cx, cz);
        }
    }

    w.trace.push("DONE".to_string());

    let expected: Vec<&str> = include_str!("feature_vectors.txt").lines().collect();
    assert!(
        expected.last() == Some(&"DONE"),
        "vector file was truncated"
    );

    let mut section = "";
    for (i, want) in expected.iter().enumerate() {
        if want.starts_with("F ") {
            section = want;
        }
        let got = w.trace.get(i).map(|s| s.as_str()).unwrap_or("<end of trace>");
        assert_eq!(got, *want, "line {} under {}", i + 1, section);
    }
    assert_eq!(w.trace.len(), expected.len(), "the port traced extra lines");
}
