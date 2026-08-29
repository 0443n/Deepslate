//! Checks the Rust Random draw for draw against vectors taken from the C++ one.
//! Regenerate the vectors with tools/gen-random-vectors.sh.

use deepslate_gen::Random;

#[test]
fn matches_cpp_vectors() {
    let text = include_str!("random_vectors.txt");
    let mut rng: Option<Random> = None;
    let mut checked = 0usize;

    for (lineno, line) in text.lines().enumerate() {
        let mut f = line.split_whitespace();
        let tag = match f.next() {
            Some(t) => t,
            None => continue,
        };
        let at = || format!("{}:{} `{}`", "random_vectors.txt", lineno + 1, line);

        if tag == "seed" {
            let seed: i64 = f.next().unwrap().parse().unwrap();
            rng = Some(Random::new(seed as i32));
            continue;
        }
        if tag == "reseed" {
            let seed: i64 = f.next().unwrap().parse().unwrap();
            rng.as_mut().unwrap().set_seed(seed as i32);
            continue;
        }
        let r = rng.as_mut().expect("vector file must open with a seed");

        match tag {
            "i" => {
                let want: i32 = f.next().unwrap().parse().unwrap();
                assert_eq!(r.next_int(), want, "next_int at {}", at());
            }
            "b" => {
                let n: i32 = f.next().unwrap().parse().unwrap();
                let want: i32 = f.next().unwrap().parse().unwrap();
                assert_eq!(r.next_int_bound(n), want, "next_int_bound({}) at {}", n, at());
            }
            "f" => {
                let want = u32::from_str_radix(f.next().unwrap(), 16).unwrap();
                assert_eq!(r.next_float().to_bits(), want, "next_float at {}", at());
            }
            "o" => {
                let want = f.next().unwrap() == "1";
                assert_eq!(r.next_boolean(), want, "next_boolean at {}", at());
            }
            "g" => {
                let want = u32::from_str_radix(f.next().unwrap(), 16).unwrap();
                assert_eq!(r.next_gaussian().to_bits(), want, "next_gaussian at {}", at());
            }
            "r" => {
                let want: i32 = f.next().unwrap().parse().unwrap();
                assert_eq!(r.next_int_bound(1000), want, "after reseed at {}", at());
            }
            other => panic!("unknown tag {} at {}", other, at()),
        }
        checked += 1;
    }

    assert!(checked > 3000, "only {} draws checked", checked);
}
