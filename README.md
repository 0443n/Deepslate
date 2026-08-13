# Minecraft PE for PSP

This repository contains a port of **Minecraft Pocket Edition** to the Sony
PlayStation Portable. It runs on **every PSP model, including the 32 MB
PSP-1000** — see [Hardware](#hardware) for what the 1000 gives up.

The base is MCPE **0.6.1** — that is where the world logic, the save format and
most of the code come from — but the feature set has since grown past it, piece
by piece, and now sits at roughly **MCPE 0.7.6**: world generation, survival and
creative, mobs, crafting, furnaces, chests, armor, TNT, the Nether Reactor,
buckets, fire and flint & steel, signs, paintings, beds and day/night, the
tripod camera, sounds and saving. There are probably still some bugs.

> [!Important]
> Join the Discord for build help, bug reports and updates on the port:
> **https://discord.gg/uQddmU7Vra**

## About the port

This is a **source-based port, not the source code itself.** The gameplay and
world logic are ported piece by piece from the original MCPE sources — 0.6.1
first, then later versions for the features 0.6.1 never had — and adapted for
the PSP, but the engine underneath is different where the hardware needs it to
be.

The biggest difference is how the map is kept in memory. MCPE holds the world
as a cache of separate chunk objects, each carrying its own block, data and
light arrays. Here the whole fixed 256×128×256 world is resident at once, so
all three had to get much smaller than a byte per block. None of them is a flat
array any more — each one pages in only the parts of the world that carry
information:

- `blocks` — block IDs in **16×16×16 sections**, the scheme Minecraft's console
  edition uses. A section that is a single ID (all air above the surface, solid
  stone below) is just that ID and costs nothing. Anything else gets a page:
  **4 bits per block** indexing a 16-entry palette, or a full byte per block if
  that section really does hold more than 16 block types — so there is no limit
  on what you can build in one place, it just costs more there. Measured across
  real saved worlds, 8 MB becomes 2.3–2.9 MB.
- `data` — block metadata at **4 bits per block**, stored sparsely: one 64-byte
  page per column, allocated on the first non-zero write. Measured on real
  worlds, ~95% of columns never hold any metadata at all, so 4 MB becomes
  about 0.6 MB.
- `light` — sky and block light as **16×16 horizontal planes** with a sentinel
  index, also from the console edition. About 95% of sky planes and 80% of
  block-light planes are uniform (all dark or all lit) and cost one index entry
  instead of a page, so 8 MB becomes under 1 MB.

Together that is roughly 20 MB of world down to about 4 MB, which is what makes
the whole map fit on a 32 MB PSP alongside the meshes.

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

Header dependencies are tracked, so editing a `.h` rebuilds everything that
includes it — a plain `make` is enough after the first build.

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
have half the memory of everything later, and the world plus its meshes still
take most of the heap. So the port detects the model at boot and makes two
things smaller there:

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

- Gameplay and world logic ported from the Minecraft Pocket Edition sources
  (0.6.1 as the base, later versions for the newer features).
- [**MCPE-0.8.1**](https://github.com/oldminecraftcommunity/MCPE-0.8.1) — the
  0.8.1 decompilation, used as a source for the features 0.6.1 never had.
- [**Oreo**](https://github.com/Oreo80) — helped with the porting.
- [**CODINGBOTSTUDIO**](https://github.com/CODINGBOTSTUDIO) — contributed the
  code the 3D clouds are based on.
- [**CYEVV**](https://github.com/CYEVV) — helped fix in-game buttons that were
  not rendering with the 4444 texture format.

## License

The original engine code written for this port — the world storage, the PSP
renderer and mesher, the GU/graphics layer, and everything else authored here
for the PSP — is released under the **GNU General Public License v3.0**
(see [LICENSE](LICENSE)). In short: use it, study it, fork it — but if you
distribute a modified version or a binary built from it, you have to release
its complete source under the same license. This covers **every version of the
project, including the earlier ones** — there is no MIT branch of it still on
offer. (Copies someone already received under the old MIT terms keep those
rights; that part is not up to anyone.)

**What the GPL does not cover:** the gameplay and world logic in this project is
ported from the Minecraft Pocket Edition 0.6.1 sources, and Minecraft is the
intellectual property of Mojang / Microsoft. That copyright, and the
"Minecraft" trademark, are theirs — the GPL grant applies only to the original
PSP engine work, not to anything derived from Mojang's code.

This is a non-commercial, educational project and is not affiliated with,
endorsed by, or associated with Mojang or Microsoft. The game assets bundled
under `data/` (textures such as `terrain.png`, sounds, the font, mob and GUI
art) are the property of Mojang / Microsoft and are not covered by the GPL
above; they are included only to make this educational port runnable.
If you are a rights holder and want anything removed, open an issue.
