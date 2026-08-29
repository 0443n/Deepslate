//! The handful of helpers levelgen uses out of src/util/mth.h.

pub const PI: f32 = 3.14159265358979323846;

pub fn floor(v: f32) -> i32 {
    let i = v as i32;
    if v < 0.0 && i as f32 != v {
        i - 1
    } else {
        i
    }
}
