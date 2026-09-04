// Head aiming. A request only lasts the tick it was made in, exactly like the
// C++ it replaces, so a goal that stops looking lets the head settle.

use super::{MobIn, MobOut};
use crate::newlib::{atan2f, sqrtf};

const RADDEG: f32 = 180.0 / core::f32::consts::PI;
/// How fast an unattended head swings back in line with the body.
const SETTLE_STEP: f32 = 10.0;

fn clamp_turn(want: f32, cur: f32, max_step: f32) -> f32 {
    let mut diff = want - cur;
    while diff < -180.0 {
        diff += 360.0;
    }
    while diff >= 180.0 {
        diff -= 360.0;
    }
    if diff > max_step {
        diff = max_step;
    }
    if diff < -max_step {
        diff = -max_step;
    }
    cur + diff
}

pub struct LookControl {
    wanted: bool,
    x: f32,
    y: f32,
    z: f32,
    max_yaw_step: f32,
    max_pitch_step: f32,
}

impl LookControl {
    pub fn new() -> LookControl {
        LookControl {
            wanted: false,
            x: 0.0,
            y: 0.0,
            z: 0.0,
            max_yaw_step: 10.0,
            max_pitch_step: 40.0,
        }
    }

    pub fn set_look_at(&mut self, x: f32, y: f32, z: f32, yaw_step: f32, pitch_step: f32) {
        self.wanted = true;
        self.x = x;
        self.y = y;
        self.z = z;
        self.max_yaw_step = yaw_step;
        self.max_pitch_step = pitch_step;
    }

    /// Aims the head. The body is steered by the navigator through `out.y_rot`,
    /// so a walking mob can still turn to watch something, the way vanilla's
    /// separate yHeadRot does.
    pub fn tick(&mut self, s: &MobIn, out: &mut MobOut) {
        if !self.wanted {
            // Nothing is looking, so the head drifts back in line with the body.
            out.y_head_rot = clamp_turn(out.y_rot, s.y_rot, SETTLE_STEP);
            out.x_rot = clamp_turn(0.0, s.x_rot, self.max_pitch_step);
            return;
        }
        self.wanted = false;

        let dx = self.x - s.x;
        let dz = self.z - s.z;
        let dy = self.y - (s.y + s.head_height);
        let horiz = sqrtf(dx * dx + dz * dz);

        let want_yaw = atan2f(dz, dx) * RADDEG - 90.0;
        let want_pitch = -(atan2f(dy, horiz) * RADDEG);

        out.y_head_rot = clamp_turn(want_yaw, s.y_rot, self.max_yaw_step);
        out.x_rot = clamp_turn(want_pitch, s.x_rot, self.max_pitch_step);
    }
}
