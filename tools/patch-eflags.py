#!/usr/bin/env python3
"""Retag rustc's o32 objects as EABI32 so psp-ld will link them.

rustc only emits o32 for mipsel-sony-psp and LLVM has no MIPS EABI backend, so
psp-ld refuses the archive on an e_flags mismatch alone. The generated code is
compatible wherever a call takes at most four integer or pointer sized
arguments and returns one, which is the rule every exported symbol in rust/
follows, so the flags are the only thing standing in the way.

Usage: patch-eflags.py <input.a> <output.a>
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile

# noreorder | allegrex | eabi32 | mips2, matching what psp-gcc writes.
PSP_EFLAGS = 0x10A23001
E_FLAGS_OFF = 0x24

AR = os.environ.get("PSP_AR", "psp-ar")


def retag(path):
    with open(path, "rb") as f:
        data = bytearray(f.read())
    if data[:4] != b"\x7fELF":
        return False
    if struct.unpack_from("<I", data, E_FLAGS_OFF)[0] == PSP_EFLAGS:
        return False
    struct.pack_into("<I", data, E_FLAGS_OFF, PSP_EFLAGS)
    with open(path, "wb") as f:
        f.write(data)
    return True


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])

    work = tempfile.mkdtemp(prefix="dsflags-")
    try:
        subprocess.run([AR, "x", src], cwd=work, check=True)
        members = sorted(os.listdir(work))
        if not members:
            sys.exit("patch-eflags: %s held no objects" % src)
        for m in members:
            retag(os.path.join(work, m))
        # ar r only replaces members by name, so a rebuild whose object hashes
        # changed would leave the previous ones behind alongside the new.
        if os.path.exists(dst):
            os.remove(dst)
        subprocess.run([AR, "rcs", dst] + members, cwd=work, check=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
