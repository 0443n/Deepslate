#ifndef DEEPSLATE_RS_H__
#define DEEPSLATE_RS_H__

// Declarations for the Rust static archive. Every signature here is limited to
// four integer or pointer arguments returning one, the only shape rustc's o32
// output and psp-gcc's EABI32 agree on. Floats cross as their bit pattern.
extern "C" {

int ds_abi_check(int a, int b, int c, int d);

}

// False when the Rust and C++ halves disagree on argument passing, which means
// the toolchain changed under us and nothing linked from Rust can be trusted.
bool rsAbiOk();

#endif
