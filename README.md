# Deepslate

Minecraft PE for the PlayStation Portable, brought forward to the modern game.
Fork of [Pencea-Flavius/Minecraft-PE-PSP](https://github.com/Pencea-Flavius/Minecraft-PE-PSP).

Runs on every PSP model. The whole 256x128x256 world is resident at once, packed
down from ~20 MB to ~4 MB, so a 32 MB PSP-1000 works too on a shorter view
distance.

## What this fork changes

- **A real Nether.** Obsidian frame, flint and steel, stand in it. Own dimension
  with its own generator, saved beside the overworld as `DIM-1`. Replaces the
  Nether Reactor.
- **Modern art.** `terrain.png`, its mip levels and the GUI sheet are composed
  from current Minecraft's default textures by the scripts in [`tools/`](tools).
  The atlases are committed; point the scripts at your own client jar to rebuild.
- **Mob spawning that works.** Animals seed at worldgen and keep spawning on lit
  grass near the player, in creative as well as survival. Hostiles from Easy up.
- **No tripod camera.**
- Renderer: visible sections collected during discovery instead of a full sweep
  per frame, frame-stamped visibility, cave cull walks through streaming chunks.
- View distance survives a trip through a portal.

## Build

PSPDEV toolchain on `PATH`:

```
make          # -> EBOOT.PBP
make dist     # -> build/, ready to copy onto the stick
```

## Install

```
PSP/GAME/DEEPSLATE/EBOOT.PBP
PSP/GAME/DEEPSLATE/data/
```

Keep the two together; worlds save into a `saves/` folder created beside them.
In PPSSPP, just open `EBOOT.PBP`.

Control scheme Layout 4 wants a DualShock 3 and Total_Noob's
[DS3Remapper](https://github.com/rereprep/VanillaDS3Remapper) in `seplugins/`.
Without it, it plays as Layout 1.

## License

Engine code is **GPLv3** (see [LICENSE](LICENSE)): fork it, but ship the source
with any binary. Not covered by that grant: the gameplay and world logic, ported
from the Minecraft Pocket Edition 0.6.1 sources, and the art under `data/`. Both
are Mojang / Microsoft's. This is a non-commercial educational project, not
affiliated with or endorsed by either.

Attributions the licenses require: the upstream port above, the
[MCPE-0.8.1](https://github.com/oldminecraftcommunity/MCPE-0.8.1) decompilation,
and [DaedalusX64](https://github.com/DaedalusX64/daedalus), whose `memcpy_vfpu`
is `src/util/fast_memcpy.cpp` essentially verbatim under GPL-2.0-or-later. The
full contributor list lives in upstream's README.
