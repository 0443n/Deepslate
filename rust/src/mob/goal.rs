// Goal arbitration, following Pumpkin's GoalSelector. Goals claim control slots
// and the lowest priority number wins a contested slot.

use super::ctx::Ctx;

pub const MOVE: u8 = 1;
pub const LOOK: u8 = 2;
pub const JUMP: u8 = 4;
pub const TARGET: u8 = 8;

const SLOTS: [u8; 4] = [MOVE, LOOK, JUMP, TARGET];

/// Ticks a duration down to the rate the selector actually scans at. Vanilla
/// calls this reducedTickDelay, and it is a ceiling divide so a 1 stays a 1.
pub fn goal_ticks(t: i32) -> i32 {
    -((-t).div_euclid(2))
}

pub trait Goal {
    fn controls(&self) -> u8 {
        0
    }

    /// Short tag for the trace, so a console session says which goals ran.
    fn name(&self) -> &'static str {
        "?"
    }

    fn can_start(&mut self, c: &mut Ctx) -> bool;

    fn should_continue(&mut self, c: &mut Ctx) -> bool {
        self.can_start(c)
    }

    /// A goal that cannot stop holds its slots even against a higher priority,
    /// which is how the creeper swell and the sheep's meal get to finish.
    fn can_stop(&self) -> bool {
        true
    }

    /// Goals tick at the selector's half rate unless they ask for every tick.
    fn should_run_every_tick(&self) -> bool {
        false
    }

    fn start(&mut self, _c: &mut Ctx) {}
    fn stop(&mut self, _c: &mut Ctx) {}
    fn tick(&mut self, _c: &mut Ctx) {}
}

struct Entry {
    goal: alloc::boxed::Box<dyn Goal>,
    priority: u8,
    running: bool,
}

use alloc::vec::Vec;

pub struct GoalSelector {
    entries: Vec<Entry>,
    by_slot: [i8; 4],
}

impl GoalSelector {
    pub fn new() -> GoalSelector {
        GoalSelector { entries: Vec::new(), by_slot: [-1; 4] }
    }

    pub fn add(&mut self, priority: i32, goal: alloc::boxed::Box<dyn Goal>) {
        self.entries.push(Entry { goal, priority: priority as u8, running: false });
    }

    fn can_replace_all(&self, candidate: usize) -> bool {
        let want = self.entries[candidate].goal.controls();
        for s in 0..4 {
            if want & SLOTS[s] == 0 {
                continue;
            }
            let holder = self.by_slot[s];
            if holder < 0 {
                continue;
            }
            let holder = holder as usize;
            if !self.entries[holder].goal.can_stop() {
                return false;
            }
            if self.entries[candidate].priority >= self.entries[holder].priority {
                return false;
            }
        }
        true
    }

    /// Writes the running goals into buf as a comma separated list.
    pub fn describe(&self, buf: &mut [u8], at: &mut usize) {
        for e in self.entries.iter() {
            if !e.running {
                continue;
            }
            for &b in e.goal.name().as_bytes() {
                if *at < buf.len() {
                    buf[*at] = b;
                    *at += 1;
                }
            }
            if *at < buf.len() {
                buf[*at] = b',';
                *at += 1;
            }
        }
    }

    pub fn tick(&mut self, c: &mut Ctx, full: bool) {
        if !full {
            for e in self.entries.iter_mut() {
                if e.running && e.goal.should_run_every_tick() {
                    e.goal.tick(c);
                }
            }
            return;
        }

        for i in 0..self.entries.len() {
            if self.entries[i].running && !self.entries[i].goal.should_continue(c) {
                self.entries[i].goal.stop(c);
                self.entries[i].running = false;
            }
        }

        for s in 0..4 {
            let h = self.by_slot[s];
            if h >= 0 && !self.entries[h as usize].running {
                self.by_slot[s] = -1;
            }
        }

        for i in 0..self.entries.len() {
            if self.entries[i].running || !self.can_replace_all(i) {
                continue;
            }
            if !self.entries[i].goal.can_start(c) {
                continue;
            }
            let want = self.entries[i].goal.controls();
            for s in 0..4 {
                if want & SLOTS[s] == 0 {
                    continue;
                }
                let h = self.by_slot[s];
                if h >= 0 {
                    self.entries[h as usize].goal.stop(c);
                    self.entries[h as usize].running = false;
                }
                self.by_slot[s] = i as i8;
            }
            self.entries[i].running = true;
            self.entries[i].goal.start(c);
        }

        for i in 0..self.entries.len() {
            if self.entries[i].running {
                self.entries[i].goal.tick(c);
            }
        }
    }
}
