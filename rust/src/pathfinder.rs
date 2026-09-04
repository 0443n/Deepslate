//! A* over the block grid, the port of src/world/level/pathfinder/.
//!
//! Node identity, the probe order of the hash table and the pop limit are all
//! kept as the C++ had them, because they decide which of several equal cost
//! paths comes back and the parity vectors pin that down.

use crate::newlib::sqrtf;

pub const MAX_NODES: usize = 2048;
pub const TABLE_SIZE: usize = 4096;
pub const MAX_PATH: usize = 64;

/// The search gives up after this many pops and returns its closest approach.
const MAX_POP_ITERS: i32 = 512;
/// A node is in the heap at most once, so the arena bounds the heap too.
const HEAP_CAP: usize = MAX_NODES;

const NONE: u16 = 0xffff;

// Free space classes, matching the C++ TYPE_ constants.
const T_FENCE: i32 = 1;
const T_LAVA: i32 = 2;
const T_WATER: i32 = 3;
const T_BLOCKED: i32 = 4;
const T_OPEN: i32 = 5;
const T_WALKABLE: i32 = 6;

// Block flags, filled in on the C++ side from Tile::tiles so the tile table
// stays the single source of truth.
pub const F_SOLID: u8 = 1;
pub const F_WATER: u8 = 2;
pub const F_LAVA: u8 = 4;
pub const F_FENCE: u8 = 8;
pub const F_DOOR: u8 = 16;

/// The two block queries the search makes, and nothing more.
pub trait PathWorld {
    fn block(&self, x: i32, y: i32, z: i32) -> i32;
    fn data(&self, x: i32, y: i32, z: i32) -> i32;
}

/// What the mob is and where it wants to go, in one piece so the boundary
/// stays inside the four argument shape the two ABIs agree on.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct PathQuery {
    pub bb_x0: f32,
    pub bb_y0: f32,
    pub bb_z0: f32,
    pub x: f32,
    pub z: f32,
    pub bb_width: f32,
    pub bb_height: f32,
    pub tx: f32,
    pub ty: f32,
    pub tz: f32,
    pub max_dist: f32,
    pub in_water: i32,
    pub avoid_water: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PathOut {
    pub len: i32,
    pub pts: [[i16; 3]; MAX_PATH],
}

impl PathOut {
    pub fn empty() -> PathOut {
        PathOut { len: 0, pts: [[0; 3]; MAX_PATH] }
    }
}

#[derive(Clone, Copy)]
struct Node {
    g: f32,
    h: f32,
    f: f32,
    came_from: u16,
    heap_idx: i16,
    x: i16,
    y: i16,
    z: i16,
    closed: bool,
}

impl Node {
    fn new(x: i32, y: i32, z: i32) -> Node {
        Node {
            g: 0.0,
            h: 0.0,
            f: 0.0,
            came_from: NONE,
            heap_idx: -1,
            x: x as i16,
            y: y as i16,
            z: z as i16,
            closed: false,
        }
    }
}

/// The C++ hash, reproduced including its overlapping fields. Collisions are
/// settled by the probe comparing coordinates, so quality only costs probes,
/// but the order it produces is what the parity vectors were taken against.
fn create_hash(x: i32, y: i32, z: i32) -> i32 {
    let mut h = (y & 0xff) | ((x & 0x7fff) << 8) | (((z & 0x7fff) as i32).wrapping_shl(24));
    if x < 0 {
        h |= 0x0080_0000;
    }
    if z < 0 {
        h |= 0x0000_8000;
    }
    h
}

fn floor_f(v: f32) -> i32 {
    let i = v as i32;
    if v < i as f32 {
        i - 1
    } else {
        i
    }
}

pub struct PathFinder {
    nodes: [Node; MAX_NODES],
    table: [u16; TABLE_SIZE],
    heap: [u16; HEAP_CAP],
    neighbors: [u16; 4],
    node_count: usize,
    heap_size: usize,
    avoid_water: bool,
    flags: [u8; 256],
}

impl PathFinder {
    pub fn new(flags: [u8; 256]) -> PathFinder {
        PathFinder {
            nodes: [Node::new(0, 0, 0); MAX_NODES],
            table: [0; TABLE_SIZE],
            heap: [0; HEAP_CAP],
            neighbors: [NONE; 4],
            node_count: 0,
            heap_size: 0,
            avoid_water: false,
            flags: [0; 256],
        }
        .with_flags(flags)
    }

    fn with_flags(mut self, flags: [u8; 256]) -> PathFinder {
        self.flags = flags;
        self
    }

    pub fn set_flags(&mut self, flags: [u8; 256]) {
        self.flags = flags;
    }

    fn flag(&self, id: i32, bit: u8) -> bool {
        self.flags[(id as u8) as usize] & bit != 0
    }

    // --- node arena ------------------------------------------------------

    fn node_at(&self, idx: u16) -> &Node {
        &self.nodes[idx as usize]
    }

    fn get_node(&mut self, x: i32, y: i32, z: i32) -> u16 {
        let hash = create_hash(x, y, z);
        let mut slot = ((hash as u32).wrapping_mul(0x9e37_79b1) as usize) & (TABLE_SIZE - 1);
        while self.table[slot] != 0 {
            let idx = self.table[slot] - 1;
            let n = &self.nodes[idx as usize];
            if n.x as i32 == x && n.y as i32 == y && n.z as i32 == z {
                return idx;
            }
            slot = (slot + 1) & (TABLE_SIZE - 1);
        }
        if self.node_count >= MAX_NODES {
            return NONE;
        }
        let idx = self.node_count;
        self.nodes[idx] = Node::new(x, y, z);
        self.node_count += 1;
        self.table[slot] = (idx + 1) as u16;
        idx as u16
    }

    fn distance(&self, a: u16, b: u16) -> f32 {
        let (a, b) = (self.node_at(a), self.node_at(b));
        let xd = (b.x - a.x) as f32;
        let yd = (b.y - a.y) as f32;
        let zd = (b.z - a.z) as f32;
        sqrtf(xd * xd + yd * yd + zd * zd)
    }

    // --- binary heap -----------------------------------------------------

    fn heap_clear(&mut self) {
        self.heap_size = 0;
    }

    fn heap_insert(&mut self, idx: u16) {
        let at = self.heap_size;
        self.heap[at] = idx;
        self.nodes[idx as usize].heap_idx = at as i16;
        self.heap_size += 1;
        self.up_heap(at);
    }

    fn heap_pop(&mut self) -> u16 {
        let popped = self.heap[0];
        self.heap_size -= 1;
        self.heap[0] = self.heap[self.heap_size];
        if self.heap_size > 0 {
            self.down_heap(0);
        }
        self.nodes[popped as usize].heap_idx = -1;
        popped
    }

    fn change_cost(&mut self, idx: u16, new_cost: f32) {
        let old_cost = self.nodes[idx as usize].f;
        self.nodes[idx as usize].f = new_cost;
        let at = self.nodes[idx as usize].heap_idx as usize;
        if new_cost < old_cost {
            self.up_heap(at);
        } else {
            self.down_heap(at);
        }
    }

    fn up_heap(&mut self, mut at: usize) {
        let idx = self.heap[at];
        let cost = self.nodes[idx as usize].f;
        while at > 0 {
            let parent_at = (at - 1) >> 1;
            let parent = self.heap[parent_at];
            if cost < self.nodes[parent as usize].f {
                self.heap[at] = parent;
                self.nodes[parent as usize].heap_idx = at as i16;
                at = parent_at;
            } else {
                break;
            }
        }
        self.heap[at] = idx;
        self.nodes[idx as usize].heap_idx = at as i16;
    }

    fn down_heap(&mut self, mut at: usize) {
        let idx = self.heap[at];
        let cost = self.nodes[idx as usize].f;
        loop {
            let left_at = 1 + (at << 1);
            let right_at = left_at + 1;
            if left_at >= self.heap_size {
                break;
            }
            let left = self.heap[left_at];
            let left_cost = self.nodes[left as usize].f;
            let (right, right_cost) = if right_at >= self.heap_size {
                (NONE, f32::MAX)
            } else {
                let r = self.heap[right_at];
                (r, self.nodes[r as usize].f)
            };
            if left_cost < right_cost {
                if left_cost < cost {
                    self.heap[at] = left;
                    self.nodes[left as usize].heap_idx = at as i16;
                    at = left_at;
                } else {
                    break;
                }
            } else if right_cost < cost {
                self.heap[at] = right;
                self.nodes[right as usize].heap_idx = at as i16;
                at = right_at;
            } else {
                break;
            }
        }
        self.heap[at] = idx;
        self.nodes[idx as usize].heap_idx = at as i16;
    }

    // --- world queries ---------------------------------------------------

    fn is_free(&self, w: &dyn PathWorld, x: i32, y: i32, z: i32, size: (i32, i32, i32)) -> i32 {
        for xx in x..x + size.0 {
            for yy in y..y + size.1 {
                for zz in z..z + size.2 {
                    let id = w.block(xx, yy, zz);
                    if id <= 0 {
                        continue;
                    }
                    if self.flag(id, F_DOOR) {
                        if w.data(xx, yy, zz) & 4 == 0 {
                            return T_BLOCKED;
                        }
                        continue;
                    } else if self.flag(id, F_WATER) {
                        if self.avoid_water {
                            return T_WATER;
                        }
                    } else if self.flag(id, F_FENCE) {
                        return T_FENCE;
                    }
                    if self.flag(id, F_SOLID) {
                        return T_BLOCKED;
                    }
                    if self.flag(id, F_LAVA) {
                        return T_LAVA;
                    }
                }
            }
        }
        T_OPEN
    }

    fn node_for(
        &mut self,
        w: &dyn PathWorld,
        x: i32,
        mut y: i32,
        z: i32,
        size: (i32, i32, i32),
        jump_size: i32,
    ) -> u16 {
        let mut best = NONE;
        let path_type = self.is_free(w, x, y, z, size);
        if path_type == T_WALKABLE {
            return self.get_node(x, y, z);
        }
        if path_type == T_OPEN {
            best = self.get_node(x, y, z);
        }
        if best == NONE
            && jump_size > 0
            && path_type != T_FENCE
            && self.is_free(w, x, y + jump_size, z, size) == T_OPEN
        {
            best = self.get_node(x, y + jump_size, z);
            y += jump_size;
        }
        if best != NONE {
            let mut drop = 0;
            let mut cost = 0;
            while y > 0 {
                cost = self.is_free(w, x, y - 1, z, size);
                if self.avoid_water && cost == T_WATER {
                    return NONE;
                }
                if cost != T_OPEN {
                    break;
                }
                drop += 1;
                if drop >= 4 {
                    return NONE;
                }
                y -= 1;
                if y > 0 {
                    best = self.get_node(x, y, z);
                }
            }
            if cost == T_LAVA {
                return NONE;
            }
        }
        best
    }

    fn get_neighbors(
        &mut self,
        w: &dyn PathWorld,
        pos: u16,
        size: (i32, i32, i32),
        target: u16,
        max_dist: f32,
    ) -> usize {
        let (px, py, pz) = {
            let n = self.node_at(pos);
            (n.x as i32, n.y as i32, n.z as i32)
        };
        let jump_size = if self.is_free(w, px, py + 1, pz, size) == T_OPEN { 1 } else { 0 };

        let cand = [
            self.node_for(w, px, py, pz + 1, size, jump_size),
            self.node_for(w, px - 1, py, pz, size, jump_size),
            self.node_for(w, px + 1, py, pz, size, jump_size),
            self.node_for(w, px, py, pz - 1, size, jump_size),
        ];

        let mut p = 0;
        for &c in cand.iter() {
            if c == NONE || self.node_at(c).closed {
                continue;
            }
            if self.distance(c, target) < max_dist {
                self.neighbors[p] = c;
                p += 1;
            }
        }
        p
    }

    // --- the search ------------------------------------------------------

    pub fn find(&mut self, w: &dyn PathWorld, q: &PathQuery, out: &mut PathOut) -> bool {
        out.len = 0;
        self.table = [0; TABLE_SIZE];
        self.node_count = 0;
        self.avoid_water = q.avoid_water != 0;

        let start_y = if q.in_water != 0 {
            let mut sy = q.bb_y0 as i32;
            while self.flag(w.block(floor_f(q.x), sy, floor_f(q.z)), F_WATER) {
                sy += 1;
            }
            sy
        } else {
            floor_f(q.bb_y0 + 0.5)
        };

        let from = self.get_node(floor_f(q.bb_x0), start_y, floor_f(q.bb_z0));

        let half = q.bb_width / 2.0;
        let xx0 = floor_f(q.tx - half);
        let yy0 = floor_f(q.ty);
        let zz0 = floor_f(q.tz - half);

        let mut to = NONE;
        if w.block(xx0, yy0 - 1, zz0) != 0 {
            to = self.get_node(xx0, yy0, zz0);
        } else {
            let xx1 = floor_f(q.tx + half);
            let zz1 = floor_f(q.tz + half);
            'outer: for xx in xx0..=xx1 {
                for zz in zz0..=zz1 {
                    if w.block(xx, yy0 - 1, zz) != 0 {
                        to = self.get_node(xx, yy0, zz);
                        break 'outer;
                    }
                }
            }
            if to == NONE {
                let mut yy = yy0;
                while w.block(xx0, yy - 1, zz0) == 0 && yy > 0 {
                    yy -= 1;
                }
                to = self.get_node(xx0, yy, zz0);
            }
        }

        let size = (
            floor_f(q.bb_width + 1.0),
            floor_f(q.bb_height + 1.0),
            floor_f(q.bb_width + 1.0),
        );
        self.find_nodes(w, from, to, size, q.max_dist, out)
    }

    fn find_nodes(
        &mut self,
        w: &dyn PathWorld,
        from: u16,
        to: u16,
        size: (i32, i32, i32),
        max_dist: f32,
        out: &mut PathOut,
    ) -> bool {
        if from == NONE || to == NONE {
            return false;
        }
        let h = self.distance(from, to);
        {
            let n = &mut self.nodes[from as usize];
            n.g = 0.0;
            n.h = h;
            n.f = h;
        }

        self.heap_clear();
        self.heap_insert(from);

        let mut closest = from;
        let mut iters = 0;

        while self.heap_size != 0 {
            iters += 1;
            if iters > MAX_POP_ITERS {
                break;
            }
            let x = self.heap_pop();
            if x == to {
                self.reconstruct(to, out);
                return true;
            }
            if self.distance(x, to) < self.distance(closest, to) {
                closest = x;
            }
            self.nodes[x as usize].closed = true;

            let n = self.get_neighbors(w, x, size, to, max_dist);
            for i in 0..n {
                let y = self.neighbors[i];
                if self.nodes[y as usize].closed {
                    continue;
                }
                let tentative_g = self.nodes[x as usize].g + self.distance(x, y);
                let in_open = self.nodes[y as usize].heap_idx >= 0;
                if !in_open || tentative_g < self.nodes[y as usize].g {
                    let hy = self.distance(y, to);
                    {
                        let ny = &mut self.nodes[y as usize];
                        ny.came_from = x;
                        ny.g = tentative_g;
                        ny.h = hy;
                    }
                    if in_open {
                        self.change_cost(y, tentative_g + hy);
                    } else {
                        self.nodes[y as usize].f = tentative_g + hy;
                        self.heap_insert(y);
                    }
                }
            }
        }

        if closest == from {
            return false;
        }
        self.reconstruct(closest, out);
        true
    }

    /// Walks the chain back from the end, then fills forward. The far end is
    /// what gets dropped when a path runs past MAX_PATH, the mob repaths long
    /// before it would have reached there.
    fn reconstruct(&self, to: u16, out: &mut PathOut) {
        let mut total = 1;
        let mut n = to;
        while self.node_at(n).came_from != NONE {
            total += 1;
            n = self.node_at(n).came_from;
        }

        let len = if total > MAX_PATH { MAX_PATH } else { total };
        out.len = len as i32;

        // Walking back from the end means the index is known without a scratch
        // list, the nodes past MAX_PATH just fall outside the array.
        let mut at = total - 1;
        n = to;
        loop {
            if at < len {
                let node = self.node_at(n);
                out.pts[at] = [node.x, node.y, node.z];
            }
            if self.node_at(n).came_from == NONE {
                break;
            }
            n = self.node_at(n).came_from;
            at -= 1;
        }
    }
}

// The one finder the whole game shares. It is pure scratch between calls and
// the console is single threaded, so a second copy would only cost 60 KB.
static mut SHARED: *mut PathFinder = core::ptr::null_mut();

pub fn shared_init(flags: [u8; 256]) {
    unsafe {
        if SHARED.is_null() {
            SHARED = alloc::boxed::Box::into_raw(alloc::boxed::Box::new(PathFinder::new(flags)));
        } else {
            (*SHARED).set_flags(flags);
        }
    }
}

pub fn shared_free() {
    unsafe {
        if !SHARED.is_null() {
            drop(alloc::boxed::Box::from_raw(SHARED));
            SHARED = core::ptr::null_mut();
        }
    }
}

pub fn shared_find(world: &dyn PathWorld, q: &PathQuery, out: &mut PathOut) -> bool {
    unsafe {
        if SHARED.is_null() {
            out.len = 0;
            return false;
        }
        (*SHARED).find(world, q, out)
    }
}

/// Asks the shared block table about one id, so callers outside the search can
/// tell water from ground without a table of their own.
pub fn shared_flag(id: i32, bit: u8) -> bool {
    unsafe {
        if SHARED.is_null() || id < 0 || id > 255 {
            return false;
        }
        (*SHARED).flags[id as usize] & bit != 0
    }
}
