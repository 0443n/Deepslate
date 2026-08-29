//! Checks the fdlibm port against sinf and cosf as the PSP actually computes
//! them. See tools/gen-trig-vectors.md for how the vectors were captured.

use deepslate_gen::fdlibm;

#[test]
fn matches_newlib_on_psp() {
    let text = include_str!("trig_vectors.txt");
    let mut checked = 0usize;
    let mut saw_done = false;

    for line in text.lines() {
        let f: Vec<&str> = line.split_whitespace().collect();
        if f.first() == Some(&"DONE") {
            saw_done = true;
            continue;
        }
        if f.len() != 4 || f[0] != "t" {
            continue;
        }
        let bits = |s: &str| u32::from_str_radix(s, 16).unwrap();
        let x = f32::from_bits(bits(f[1]));

        // Above 2^7*(pi/2) the port hands off to the libm crate on purpose, so
        // those samples are outside what this test can hold it to.
        if (x.to_bits() & 0x7fff_ffff) > 0x4349_0f80 {
            continue;
        }

        assert_eq!(
            fdlibm::sinf(x).to_bits(),
            bits(f[2]),
            "sinf({}) [{:08x}]",
            x,
            x.to_bits()
        );
        assert_eq!(
            fdlibm::cosf(x).to_bits(),
            bits(f[3]),
            "cosf({}) [{:08x}]",
            x,
            x.to_bits()
        );
        checked += 1;
    }

    assert!(saw_done, "vector capture was truncated");
    assert!(checked > 13000, "only {} samples checked", checked);
}
