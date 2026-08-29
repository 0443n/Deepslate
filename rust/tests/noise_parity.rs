//! Checks the Rust noise port against vectors taken from ImprovedNoise.cpp and
//! PerlinNoise.cpp. Regenerate them with tools/gen-vectors.sh.

use deepslate_gen::{ImprovedNoise, PerlinNoise, Random};

const COORDS: [f32; 11] = [
    0.0, 0.5, 1.0, -0.5, -1.0, 3.25, -7.75, 255.5, 256.5, -256.5, 1000.125,
];

/// Hands back the next value tagged `tag`, insisting on the order the C++ wrote
/// them so a desynchronised reader cannot pass by accident.
struct Vectors<'a> {
    lines: core::str::Lines<'a>,
    lineno: usize,
}

impl<'a> Vectors<'a> {
    fn new(text: &'a str) -> Vectors<'a> {
        Vectors { lines: text.lines(), lineno: 0 }
    }

    fn next_row(&mut self) -> Option<(String, Vec<String>)> {
        for line in self.lines.by_ref() {
            self.lineno += 1;
            let mut f = line.split_whitespace();
            if let Some(tag) = f.next() {
                return Some((tag.to_string(), f.map(|s| s.to_string()).collect()));
            }
        }
        None
    }

    fn expect(&mut self, tag: &str) -> Vec<String> {
        let (got, fields) = self.next_row().expect("vectors ran out");
        assert_eq!(got, tag, "expected `{}` at line {}", tag, self.lineno);
        fields
    }

    fn expect_bits(&mut self, tag: &str) -> u32 {
        let f = self.expect(tag);
        u32::from_str_radix(&f[0], 16).unwrap()
    }
}

fn assert_bits(got: f32, want: u32, what: &str, line: usize) {
    assert_eq!(
        got.to_bits(),
        want,
        "{} at vector line {}, got {} want {}",
        what,
        line,
        got,
        f32::from_bits(want)
    );
}

#[test]
fn matches_cpp_vectors() {
    let mut v = Vectors::new(include_str!("noise_vectors.txt"));
    let mut checked = 0usize;

    // ImprovedNoise blocks come first, one per seed.
    for _ in 0..6 {
        let head = v.expect("improved");
        let seed: i64 = head[0].parse().unwrap();
        let mut rng = Random::new(seed as i32);
        let n = ImprovedNoise::new(&mut rng);

        for (i, want) in head[1..4].iter().enumerate() {
            let want = u32::from_str_radix(want, 16).unwrap();
            let got = [n.xo, n.yo, n.zo][i];
            assert_bits(got, want, "offset", v.lineno);
        }

        for &x in COORDS.iter() {
            for &y in COORDS.iter() {
                for k in (0..COORDS.len()).step_by(3) {
                    let line = v.lineno;
                    let want = v.expect_bits("n");
                    assert_bits(n.noise(x, y, COORDS[k]), want, "noise", line);
                    checked += 1;
                }
            }
        }

        let mut flat = [0.0f32; 8 * 6];
        n.add(&mut flat, 3.0, 10.0, -5.0, 8, 1, 6, 0.25, 1.0, 0.5, 2.0);
        for got in flat.iter() {
            let line = v.lineno;
            let want = v.expect_bits("a");
            assert_bits(*got, want, "add flat", line);
            checked += 1;
        }

        let mut vol = [0.0f32; 5 * 9 * 5];
        n.add(&mut vol, -2.0, 0.0, 4.0, 5, 9, 5, 0.5, 0.125, 0.5, 1.5);
        for got in vol.iter() {
            let line = v.lineno;
            let want = v.expect_bits("v");
            assert_bits(*got, want, "add volume", line);
            checked += 1;
        }
    }

    // Then PerlinNoise, seed outer and level count inner.
    for _ in 0..6 {
        for levels in [2usize, 4, 8, 10, 16] {
            let head = v.expect("perlin");
            let seed: i64 = head[0].parse().unwrap();
            assert_eq!(head[1].parse::<usize>().unwrap(), levels, "level order");
            let mut rng = Random::new(seed as i32);
            checked += match levels {
                2 => check_perlin(PerlinNoise::<2>::new(&mut rng), &mut v),
                4 => check_perlin(PerlinNoise::<4>::new(&mut rng), &mut v),
                8 => check_perlin(PerlinNoise::<8>::new(&mut rng), &mut v),
                10 => check_perlin(PerlinNoise::<10>::new(&mut rng), &mut v),
                _ => check_perlin(PerlinNoise::<16>::new(&mut rng), &mut v),
            };
        }
    }

    assert!(v.next_row().is_none(), "vectors left unread");
    assert!(checked > 30000, "only {} values checked", checked);
}

fn check_perlin<const L: usize>(pn: PerlinNoise<L>, v: &mut Vectors) -> usize {
    let mut checked = 0usize;

    for i in -4..=4 {
        for j in -4..=4 {
            let line = v.lineno;
            let want = v.expect_bits("p2");
            assert_bits(pn.get_value2(i as f32 * 1.5, j as f32 * 0.75), want, "getValue2", line);
            checked += 1;
        }
    }

    for i in -3..=3 {
        for j in -3..=3 {
            for k in -3..=3 {
                let line = v.lineno;
                let want = v.expect_bits("p3");
                let got = pn.get_value3(i as f32 * 1.25, j as f32 * 0.5, k as f32 * 2.0);
                assert_bits(got, want, "getValue3", line);
                checked += 1;
            }
        }
    }

    let mut region = [0.0f32; 5 * 17 * 5];
    pn.get_region(&mut region, 1.0, 2.0, 3.0, 5, 17, 5, 0.5, 0.25, 0.5);
    for got in region.iter() {
        let line = v.lineno;
        let want = v.expect_bits("pr");
        assert_bits(*got, want, "getRegion", line);
        checked += 1;
    }

    let mut flat = [0.0f32; 16 * 16];
    pn.get_region_flat(&mut flat, -32, 48, 16, 16, 0.03125, 0.03125);
    for got in flat.iter() {
        let line = v.lineno;
        let want = v.expect_bits("pf");
        assert_bits(*got, want, "getRegion flat", line);
        checked += 1;
    }

    checked
}
