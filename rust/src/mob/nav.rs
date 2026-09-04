// Path following. The search itself is rust/src/pathfinder.rs, this walks the
// nodes it returns and turns them into the movement intent for the tick.

use super::{MobIn, MobOut};
use crate::newlib::atan2f;
use crate::pathfinder::{PathOut, PathQuery, PathWorld, MAX_PATH};

const RADDEG: f32 = 180.0 / core::f32::consts::PI;
const MAX_TURN: f32 = 30.0;
/// A mob that has not covered this much ground in that many ticks is walking
/// into something the path went through, so the path gets dropped.
const STUCK_TICKS: i32 = 40;
const STUCK_DIST_SQ: f32 = 1.0;

pub struct Navigation {
    pts: [[i16; 3]; MAX_PATH],
    len: i16,
    index: i16,
    speed: f32,
    max_distance: f32,
    avoid_water: bool,
    stuck_x: f32,
    stuck_y: f32,
    stuck_z: f32,
    stuck_timer: i32,
    /// Where the body is pointing. Kept here rather than read back from the
    /// snapshot, because that field carries the head now and the head is free
    /// to look somewhere else entirely.
    yaw: f32,
}

impl Navigation {
    pub fn new() -> Navigation {
        Navigation {
            pts: [[0; 3]; MAX_PATH],
            len: 0,
            index: 0,
            speed: 0.7,
            max_distance: 16.0,
            avoid_water: false,
            stuck_x: 0.0,
            stuck_y: 0.0,
            stuck_z: 0.0,
            stuck_timer: 0,
            yaw: 0.0,
        }
    }

    pub fn yaw(&self) -> f32 {
        self.yaw
    }

    pub fn progress(&self) -> (i32, i32) {
        (self.index as i32, self.len as i32)
    }

    pub fn is_done(&self) -> bool {
        self.len == 0
    }

    pub fn is_in_progress(&self) -> bool {
        self.len != 0
    }

    pub fn stop(&mut self) {
        self.len = 0;
        self.index = 0;
    }

    pub fn set_avoid_water(&mut self, v: bool) {
        self.avoid_water = v;
    }

    pub fn last_point(&self) -> Option<[i16; 3]> {
        if self.len == 0 {
            None
        } else {
            Some(self.pts[self.len as usize - 1])
        }
    }

    fn reset_stuck(&mut self, s: &MobIn) {
        self.stuck_x = s.x;
        self.stuck_y = s.y;
        self.stuck_z = s.z;
        self.stuck_timer = 0;
    }

    pub fn move_to(
        &mut self,
        s: &MobIn,
        world: &dyn PathWorld,
        tx: f32,
        ty: f32,
        tz: f32,
        speed: f32,
    ) -> bool {
        self.speed = speed;
        let q = PathQuery {
            bb_x0: s.bb_x0,
            bb_y0: s.bb_y0,
            bb_z0: s.bb_z0,
            x: s.x,
            z: s.z,
            bb_width: s.bb_width,
            bb_height: s.bb_height,
            tx,
            ty,
            tz,
            max_dist: self.max_distance,
            in_water: s.in_water,
            avoid_water: if self.avoid_water { 1 } else { 0 },
        };
        let mut out = PathOut { len: 0, pts: [[0; 3]; MAX_PATH] };
        let found = crate::pathfinder::shared_find(world, &q, &mut out);
        let was_idle = self.len == 0;
        self.stop();
        if was_idle {
            self.yaw = s.y_body_rot;
        }
        if found && out.len > 0 {
            let n = out.len as usize;
            self.pts[..n].copy_from_slice(&out.pts[..n]);
            self.len = n as i16;
        }
        self.reset_stuck(s);
        self.is_in_progress()
    }

    /// Node centres sit on the block corner, so the mob's own width shifts them
    /// back to where its middle should pass.
    fn node_pos(&self, s: &MobIn, i: usize) -> (f32, f32, f32) {
        let half = ((s.bb_width + 1.0) as i32) as f32 * 0.5;
        let p = self.pts[i];
        (p[0] as f32 + half, p[1] as f32, p[2] as f32 + half)
    }

    pub fn tick(&mut self, s: &MobIn, out: &mut MobOut) {
        if self.len == 0 {
            return;
        }

        self.stuck_timer += 1;
        if self.stuck_timer >= STUCK_TICKS {
            let dx = s.x - self.stuck_x;
            let dy = s.y - self.stuck_y;
            let dz = s.z - self.stuck_z;
            if dx * dx + dy * dy + dz * dz < STUCK_DIST_SQ {
                self.stop();
                return;
            }
            self.reset_stuck(s);
        }

        let y_floor = crate::mth::floor(s.bb_y0 + 0.5) as f32;

        // Skip the nodes already reached, the mob is wider than one block.
        let r = s.bb_width * 2.0;
        loop {
            let (nx, ny, nz) = self.node_pos(s, self.index as usize);
            let dx = nx - s.x;
            let dz = nz - s.z;
            if dx * dx + dz * dz >= r * r {
                let dy = ny - y_floor;
                let want = atan2f(dz, dx) * RADDEG - 90.0;
                let mut diff = want - self.yaw;
                while diff < -180.0 {
                    diff += 360.0;
                }
                while diff >= 180.0 {
                    diff -= 360.0;
                }
                if diff > MAX_TURN {
                    diff = MAX_TURN;
                }
                if diff < -MAX_TURN {
                    diff = -MAX_TURN;
                }
                self.yaw += diff;
                out.y_rot = self.yaw;
                out.yya = self.speed;
                // Only the path's own step up is worth a jump. Hopping at every
                // wall the mob touches is what made them bounce off a ledge.
                if dy > 0.0 {
                    out.jumping = 1;
                }
                return;
            }
            self.index += 1;
            if self.index >= self.len {
                self.stop();
                return;
            }
        }
    }
}
