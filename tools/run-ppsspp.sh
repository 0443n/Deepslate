#!/usr/bin/env bash
# Build and run MCPSP in PPSSPP. The emulator's memstick GAME dir is symlinked
# at the project root, so a rebuild is picked up without copying anything.
set -e
cd "$(dirname "$0")/.."
export PATH=/opt/pspdev/bin:$PATH
[ "$1" = "-n" ] || make -j8
exec PPSSPPSDL --windowed --escape-exit --pause-menu-exit "$PWD/EBOOT.PBP" "${@:2}"
