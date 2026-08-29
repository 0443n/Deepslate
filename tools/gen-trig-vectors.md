# Capturing rust/tests/trig_vectors.txt

`rust/src/fdlibm.rs` is a port of newlib's `sinf` and `cosf`, the two the PSP
actually links (they are plain fdlibm, not VFPU - `psp-objdump -d` on any binary
using them shows `__ieee754_rem_pio2f` and `__kernel_sinf`). The `libm` crate is
musl-derived and disagrees with them by 1 ulp on roughly 10% of samples, which is
enough to move terrain, so the port has to be checked against the real thing.

The reference values therefore come off the console's own libm, run under the
emulator. `tools/gen-trig-vectors.c` is the harness. It prints one `t <x> <sinf>
<cosf>` line per sample as raw hex bit patterns and a final `DONE`, which the
test asserts on so a truncated capture cannot pass silently.

## Rebuilding the vectors

```sh
export PATH=/opt/pspdev/bin:$PATH
mkdir -p /tmp/trig && cp tools/gen-trig-vectors.c /tmp/trig/main.c
cat > /tmp/trig/Makefile <<'MAK'
TARGET = trig
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
LIBS = -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = trig
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
MAK
make -C /tmp/trig

script -qec "PPSSPPHeadless /tmp/trig/trig.elf -r /tmp/trig --timeout=120 --graphics=software -v -l" /tmp/trig/run.log
tr -d '\r' < /tmp/trig/run.log | sed -n 's/^I stdout: //p' > rust/tests/trig_vectors.txt
```

Two details the emulator forces. Output has to go through `printf`, not
`pspDebugScreenPrintf`, which writes to the framebuffer; and PPSSPPHeadless
suppresses stdout when it is piped, hence the `script` wrapper and `-l`. The ELF
path must be absolute with `-r` pointing at its directory, or loading fails with
`Failed to load executable umd0:/...`.

## What is sampled

Dense sweeps over the argument ranges levelgen produces, the exact expressions in
caves, ore, clay and the nether ripple, the full span the medium reduction covers
out to 2^7*(pi/2), and then bit-by-bit neighbourhoods around every threshold
fdlibm branches on and around every multiple of pi/2 in the `npio2_hw` table.
14421 samples, of which the test checks the 14405 at or below `0x43490f80`.
Above that the port defers to the `libm` crate rather than carrying
`kernel_rem_pio2f` and its 66 word table, so those samples are out of contract.
