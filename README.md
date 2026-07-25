# Minecraft PE for PSP

This repository contains a port of **Minecraft Pocket Edition 0.6.1** to the
Sony PlayStation Portable. It runs on **every PSP model, including the 32 MB
PSP-1000** — see [Hardware](#hardware) for what the 1000 gives up.

It has everything MCPE 0.6.1 has — world generation, survival and creative,
mobs, crafting, furnaces, chests, armor, TNT, the Nether Reactor, day/night,
saving — with the same look and behaviour. There are probably still some bugs.

## About the port

This is a **source-based port, not the source code itself.** The gameplay and
world logic are ported piece by piece from the original MCPE 0.6.1 sources and
adapted for the PSP, but the engine underneath is different where the hardware
needs it to be.

The biggest difference is how the map is kept in memory. MCPE holds the world
as a cache of separate chunk objects, each carrying its own block, data and
light arrays. Here the whole fixed 256×128×256 world is resident at once, so
the three arrays it needs had to get much smaller than a byte per block:

- `blocks` — one flat 8 MB array of block IDs. Read on every mesh, light,
  physics and raycast step, so it stays uncompressed on purpose.
- `data` — block metadata at **4 bits per block**, stored sparsely: one 64-byte
  page per column, allocated on the first non-zero write. Measured on real
  worlds, ~95% of columns never hold any metadata at all, so 4 MB becomes
  about 0.6 MB.
- `light` — sky and block light as **16×16 horizontal planes** with a sentinel
  index, the scheme Minecraft's console edition uses. About 95% of sky planes
  and 80% of block-light planes are uniform (all dark or all lit) and cost one
  index entry instead of a page, so 8 MB becomes under 1 MB.

The world is generated once at load around the spawn point, and the rest builds
lazily as you walk toward it. Only the mesh columns near the camera are drawn.
So it is the same *fixed* MCPE world, just held and streamed differently.

## Building

Make sure you have the [PSPDEV](https://github.com/pspdev/pspdev) toolchain on
your `PATH`, then:

```
make clean && make
```

This produces `EBOOT.PBP`. To get a ready-to-copy folder instead:

```
make dist
```

> The Makefile does not track header dependencies — after editing any `.h`, run
> `make clean && make`, or a stale object file will crash on hardware.

## Running

**On a PSP** — copy onto the memory stick so you have:

```
PSP/GAME/MCPSP/EBOOT.PBP
PSP/GAME/MCPSP/data/
```

and launch it from the Game menu. Worlds save into a `saves/` folder created
next to the EBOOT.

**In PPSSPP** — just open `EBOOT.PBP`.

Keep `EBOOT.PBP` and `data/` together; textures and sounds load from `data/`
next to the EBOOT.

## Hardware

Every PSP model runs it, but the 32 MB machines (PSP-1000, the original "Phat")
have half the memory of everything later, and the world alone is about 10 MB.
So the port detects the model at boot and makes two things smaller there:

- **Render distance** — Tiny and Short, no Normal, and it starts on Tiny. Short
  is fine on most worlds; heavy caves and lava run the heap to the edge, where
  distant sections simply stop building until you get closer.
- **Sound** — a half-rate pack (11 kHz instead of 22 kHz). It is audibly
  grainier through the speaker and saves 0.8 MB.

Nothing else differs: same world size, same generation, same gameplay, same
save files. A PSP-2000 or later uses the full-size sound pack and all three
render distances.

## Compatibility

Worlds use the real MCPE 0.6.1 on-disk format (`chunks.dat`, `level.dat`,
`entities.dat`). A world made on the PSP opens in MCPE 0.6.1, and a world copied
off a phone opens on the PSP.

## Credits

- Gameplay and world logic ported from the Minecraft Pocket Edition 0.6.1
  sources.
- [**Oreo**](https://github.com/Oreo80) — helped with the porting.
- [**CYEVV**](https://github.com/CYEVV) — helped fix in-game buttons that were
  not rendering with the 4444 texture format.

## License

The original engine code written for this port — the world storage, the PSP
renderer and mesher, the GU/graphics layer, and everything else authored here
for the PSP — is released under the MIT License (see [LICENSE](LICENSE)).

**What MIT does not cover:** the gameplay and world logic in this project is
ported from the Minecraft Pocket Edition 0.6.1 sources, and Minecraft is the
intellectual property of Mojang / Microsoft. That copyright, and the
"Minecraft" trademark, are theirs — the MIT grant applies only to the original
PSP engine work, not to anything derived from Mojang's code.

This is a non-commercial, educational project and is not affiliated with,
endorsed by, or associated with Mojang or Microsoft. The game assets bundled
under `data/` (textures such as `terrain.png`, sounds, the font, mob and GUI
art) are the property of Mojang / Microsoft and are not covered by the MIT
license above; they are included only to make this educational port runnable.
If you are a rights holder and want anything removed, open an issue.
