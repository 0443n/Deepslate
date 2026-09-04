//! Replays tools/gen-path-vectors.cpp against the Rust pathfinder and compares
//! the paths node for node. The obstacle world is rebuilt here to match, so any
//! difference is in the search itself, in its tie breaking, or in the order the
//! hash table hands nodes back.

use deepslate_gen::pathfinder::{
    PathFinder, PathOut, PathQuery, PathWorld, F_DOOR, F_FENCE, F_LAVA, F_SOLID, F_WATER,
};

const W: i32 = 64;
const H: i32 = 32;
const D: i32 = 64;

const AIR: u8 = 0;
const STONE: u8 = 1;
const GRASS: u8 = 2;
const DIRT: u8 = 3;
const WATER: u8 = 8;
const LAVA: u8 = 10;
const DOOR_WOOD: u8 = 64;
const FENCE: u8 = 85;

struct TestWorld {
    id: Vec<u8>,
    data: Vec<u8>,
}

fn inside(x: i32, y: i32, z: i32) -> bool {
    x >= 0 && x < W && y >= 0 && y < H && z >= 0 && z < D
}
fn at(x: i32, y: i32, z: i32) -> usize {
    ((y * D + z) * W + x) as usize
}

impl TestWorld {
    fn new() -> TestWorld {
        let n = (W * H * D) as usize;
        TestWorld { id: vec![0; n], data: vec![0; n] }
    }
    fn put(&mut self, x: i32, y: i32, z: i32, id: u8, data: u8) {
        if !inside(x, y, z) {
            return;
        }
        self.id[at(x, y, z)] = id;
        self.data[at(x, y, z)] = data;
    }
}

impl PathWorld for TestWorld {
    fn block(&self, x: i32, y: i32, z: i32) -> i32 {
        if inside(x, y, z) { self.id[at(x, y, z)] as i32 } else { 0 }
    }
    fn data(&self, x: i32, y: i32, z: i32) -> i32 {
        if inside(x, y, z) { self.data[at(x, y, z)] as i32 } else { 0 }
    }
}

fn build_world() -> TestWorld {
    let mut w = TestWorld::new();

    for z in 0..D {
        for x in 0..W {
            for y in 0..=8 {
                w.put(x, y, z, if y == 8 { GRASS } else { STONE }, 0);
            }
        }
    }

    for z in 4..28 {
        w.put(20, 9, z, STONE, 0);
    }
    for z in 4..28 {
        w.put(34, 9, z, STONE, 0);
        w.put(34, 10, z, STONE, 0);
    }

    w.put(20, 9, 16, AIR, 0);
    w.put(34, 9, 22, AIR, 0);
    w.put(34, 10, 22, AIR, 0);

    for z in 34..42 {
        for x in 6..16 {
            w.put(x, 8, z, DIRT, 0);
            w.put(x, 9, z, WATER, 0);
        }
    }
    for z in 34..40 {
        for x in 24..30 {
            w.put(x, 8, z, DIRT, 0);
            w.put(x, 9, z, LAVA, 0);
        }
    }
    for x in 38..56 {
        w.put(x, 9, 36, FENCE, 0);
    }
    w.put(46, 9, 36, DOOR_WOOD, 4);
    w.put(46, 10, 36, DOOR_WOOD, 8);

    for i in 0..6 {
        for z in 48..56 {
            for y in 9..=9 + i {
                w.put(44 + i, y, z, STONE, 0);
            }
        }
    }
    for z in 48..60 {
        for x in 4..14 {
            w.put(x, 8, z, AIR, 0);
            w.put(x, 4, z, STONE, 0);
        }
    }
    w
}

/// The table the C++ hands over at startup, built from the same predicates
/// tools/pathtest/world/level/chunk/chunk.h uses.
fn block_flags() -> [u8; 256] {
    let mut f = [0u8; 256];
    for id in [STONE, GRASS, DIRT, FENCE] {
        f[id as usize] |= F_SOLID;
    }
    for id in [8u8, 9u8] {
        f[id as usize] |= F_WATER;
    }
    for id in [10u8, 11u8] {
        f[id as usize] |= F_LAVA;
    }
    for id in [85u8, 107u8] {
        f[id as usize] |= F_FENCE;
    }
    for id in [64u8, 71u8] {
        f[id as usize] |= F_DOOR;
    }
    f
}

struct Q {
    ex: f32,
    ey: f32,
    ez: f32,
    width: f32,
    height: f32,
    in_water: bool,
    avoid_water: bool,
    tx: i32,
    ty: i32,
    tz: i32,
    max_dist: f32,
}

const SMALL_W: f32 = 0.6;
const SMALL_H: f32 = 1.8;
const BIG_W: f32 = 1.4;
const BIG_H: f32 = 1.6;

fn queries() -> Vec<Q> {
    let q = |ex, ey, ez, width, height, in_water, avoid_water, tx, ty, tz, max_dist| Q {
        ex, ey, ez, width, height, in_water, avoid_water, tx, ty, tz, max_dist,
    };
    vec![
        q(4.5, 9.0, 4.5, SMALL_W, SMALL_H, false, false, 16, 9, 4, 32.0),
        q(4.5, 9.0, 16.5, SMALL_W, SMALL_H, false, false, 30, 9, 16, 40.0),
        q(24.5, 9.0, 10.5, SMALL_W, SMALL_H, false, false, 40, 9, 10, 40.0),
        q(24.5, 9.0, 21.5, SMALL_W, SMALL_H, false, false, 40, 9, 22, 40.0),
        q(4.5, 9.0, 37.5, SMALL_W, SMALL_H, false, false, 18, 9, 37, 32.0),
        q(4.5, 9.0, 37.5, SMALL_W, SMALL_H, false, true, 18, 9, 37, 32.0),
        q(10.5, 9.0, 37.5, SMALL_W, SMALL_H, true, false, 20, 9, 37, 32.0),
        q(20.5, 9.0, 36.5, SMALL_W, SMALL_H, false, false, 33, 9, 36, 32.0),
        q(42.5, 9.0, 32.5, SMALL_W, SMALL_H, false, false, 42, 9, 40, 32.0),
        q(40.5, 9.0, 51.5, SMALL_W, SMALL_H, false, false, 52, 15, 51, 32.0),
        q(2.5, 9.0, 53.5, SMALL_W, SMALL_H, false, false, 16, 9, 53, 32.0),
        q(24.5, 9.0, 21.5, BIG_W, BIG_H, false, false, 40, 9, 22, 40.0),
        q(4.5, 9.0, 16.5, BIG_W, BIG_H, false, false, 30, 9, 16, 40.0),
        q(8.5, 9.0, 50.5, SMALL_W, SMALL_H, false, false, 9, 12, 53, 32.0),
        q(4.5, 9.0, 4.5, SMALL_W, SMALL_H, false, false, 60, 9, 60, 8.0),
    ]
}

/// findPath(Path*, Entity*, int, int, int, float) offsets an integer target to
/// the middle of the block before the search sees it.
fn to_query(q: &Q) -> PathQuery {
    PathQuery {
        bb_x0: q.ex - q.width / 2.0,
        bb_y0: q.ey,
        bb_z0: q.ez - q.width / 2.0,
        x: q.ex,
        z: q.ez,
        bb_width: q.width,
        bb_height: q.height,
        tx: q.tx as f32 + 0.5,
        ty: q.ty as f32 + 0.5,
        tz: q.tz as f32 + 0.5,
        max_dist: q.max_dist,
        in_water: q.in_water as i32,
        avoid_water: q.avoid_water as i32,
    }
}

#[derive(Debug, PartialEq)]
struct Expected {
    found: bool,
    pts: Vec<[i16; 3]>,
}

fn parse_vectors(text: &str) -> Vec<Expected> {
    let mut out: Vec<Expected> = Vec::new();
    for line in text.lines() {
        let f: Vec<&str> = line.split_whitespace().collect();
        match f.first() {
            Some(&"Q") => out.push(Expected {
                found: f[2] == "1",
                pts: Vec::new(),
            }),
            Some(&"P") => {
                let last = out.last_mut().expect("P line before any Q line");
                last.pts.push([
                    f[2].parse().unwrap(),
                    f[3].parse().unwrap(),
                    f[4].parse().unwrap(),
                ]);
            }
            _ => {}
        }
    }
    out
}

#[test]
fn matches_cpp_paths() {
    let world = build_world();
    let expected = parse_vectors(include_str!("path_vectors.txt"));
    let qs = queries();
    assert_eq!(expected.len(), qs.len(), "vector file and query list disagree");

    let mut finder = PathFinder::new(block_flags());
    let mut out = PathOut::empty();

    for (i, q) in qs.iter().enumerate() {
        let found = finder.find(&world, &to_query(q), &mut out);
        let got: Vec<[i16; 3]> = (0..out.len as usize).map(|k| out.pts[k]).collect();

        assert_eq!(found, expected[i].found, "query {} found flag", i);
        assert_eq!(got.len(), expected[i].pts.len(), "query {} path length", i);
        assert_eq!(got, expected[i].pts, "query {} path", i);
    }
}
