# Deepslate

Minecraft (Beta / MCPE era) for the PSP. C++ built with `psp-g++` from pspdev,
with the codebase being rewritten into Rust one subsystem at a time.

## The direction of the project

**Deepslate is migrating from C++ to Rust, subsystem by subsystem.** Worldgen
went first and now runs entirely from `rust/`. Everything still in C++ is
waiting its turn, not settled.

So when a subsystem is being worked on, the default question is whether this is
the moment to move it to Rust, and the answer is usually yes. Do not add a
substantial new C++ subsystem without raising it first. Small changes to code
that is still C++ are fine.

**Mobs are Rust.** Everything mob related, the AI, the goals, the navigation
and the per mob behaviour, lives in `rust/src/mob/`, and spawning lives in
`rust/src/spawner.rs`. Do not fix a mob bug by editing C++, fix it in Rust. C++
keeps entity storage, physics, collision and rendering.

When a subsystem moves to Rust the C++ it replaced is deleted in the same pass,
unless it is kept as the parity reference described under "Parity when porting".
Two live copies of the same logic is the state the migration exists to end.

The boundary is one snapshot in and one set of intents out, per mob per tick,
built and applied by `src/world/entity/path_finder_mob.cpp`. `DsMobIn` and
`DsMobOut` are written by hand on both sides, so `static_assert`s in that file
and `const _: () = assert!` in `rust/src/mob/mod.rs` pin their sizes. Adding a
field means adding it in both places, and the build says so if you do not.

C++ keeps only the per mob hooks the AI asks about or asks for, all virtuals on
`PathfinderMob`: `getAiKind`, `getTemptItem`, `canAttack`, `shouldKeepTarget`,
`doHurtTarget`, `performRangedAttack`, `ate`, `setSwellDir`, `alerted`. That is where
things like the pig zombie's anger and the spider's daylight rule live, because
they read state the snapshot has no business carrying.

Pumpkin MC (`github.com/Pumpkin-MC/Pumpkin`) is the reference consulted during
each rewrite, and it is useful precisely because it is Rust. Read it when
porting a subsystem to see what it can offer for that subsystem. It is GPL-3.0
and Deepslate is not, so take the architecture and the approach, never the
code.

## Deploying to the console

`tools/deploy.sh` owns the whole round trip. Never copy files to the card by
hand, the script keeps the rollback and the trace history that manual copying
loses.

```
make deploy                    # build, sync EBOOT + data, stamp version.txt
make trace                     # pull the trace log only
tools/deploy.sh --eboot-only   # skip the 90 MB data/ sync, the usual case
tools/deploy.sh --rollback     # restore EBOOT.prev.PBP
tools/deploy.sh --eject        # unmount when done
tools/deploy.sh -n "note"      # add a note line to version.txt
```

It finds the card by scanning removable partitions for a `PSP/GAME` directory
and mounts it through udisks, so the mountpoint is never hardcoded. Before it
overwrites anything it copies `deepslate_trace.txt` into `logs/` stamped with
the file's own mtime, which is the only run history that exists. A failed
`make` aborts before the card is touched.

The install lives at `ms0:/PSP/GAME/MCPSP/`, not `DEEPSLATE` as the generated
README says. `EBOOT.stock.PBP` there is the pre-Rust build, kept as a floor to
fall back to.

Ask before deploying if the card is not already mounted, and say so plainly
when a build is compile-verified only rather than run on hardware.

## Budgets that fail silently

- `Entity::operator new` serves a pool of 96 slots of `ENTITY_SLOT` = 2560
  bytes and falls back to `malloc` for anything larger, with no diagnostic.
  `mob_factory.cpp` guards every spawnable mob with `CHECK_FITS_POOL`. Add the
  assert whenever a mob class is added.
- `PSP_LARGE_MEMORY = 1`, so the ceiling is about 50 MB. A 22 MB heap reading
  is normal, not an OOM.
- `WORLD_SIZE_CHUNKS` defaults to 16 in `src/world/level/world.h`, but the
  build on the card is 128. Saves are not portable between the two, so check
  which one you are building before deploying over existing worlds.

## Parity when porting

Every subsystem moved to Rust is checked against the C++ it replaces, not just
eyeballed. `tools/gen-vectors.sh` builds the old C++ on the host and dumps its
output to `rust/tests/*_vectors.txt`, and a test in `rust/tests/` replays the
same script through the Rust and compares.

The replaced C++ stays in the tree as the reference the generator compiles,
even once it is out of the Makefile. Say so in a comment at the top of the
bridge that replaced it. Where the C++ is gone for good the vector file is a
golden file, and a deliberate change means reviewing its diff by hand.

Stub worlds for the host builds live under `tools/`, see `tools/pathtest`.

## Testing

PPSSPP passes things real hardware does not, data races in particular. An
emulator run is evidence a build boots, never evidence it is correct. Anything
touching threading needs a console round trip.

## The Rust boundary

`rust/` is the `deepslate-gen` staticlib, linked as `libdeepslate_psp.a`. It
needs nightly-2026-05-30.

The pattern to follow is `rust/src/psp_world.rs`. Rust declares a trait for
what it needs from the game, and one implementation forwards to C++ through
`extern "C"` shims that live in `src/rs/rs.cpp` and are declared in `src/rs/rs.h`.
The C++ side keeps thin entry points, see `gen_bridge.cpp`.

Worldgen tolerated chatty shims because it runs once per chunk. Anything on the
per-tick path needs a coarser boundary, a snapshot in and an intent out rather
than a getter per field.

Host builds of the parity vectors are valid because the generator is integer
and bit exact. Trig is the one exception.
