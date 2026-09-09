# Arena mode

Arena mode is a 3D mecha duel built on ROLLER's software rasteriser and its
existing math conventions. It reuses the renderer, the 14-bit heading circle,
the world scale and the frontend state machine; everything else -- movement,
weapons, collision, the machines themselves and the arenas -- is new code in
`PROJECTS/ROLLER/mecha_*`.

It is an original game. What it takes from the arcade twin-stick mecha lineage
is the shape of the mechanics, not anyone's machines, artwork or data: the
roster, the arenas, the balance figures and the geometry are all written for
this project.

## Running it

```sh
zig build run -- --arena
zig build run -- --arena-mech 3 --arena-foe 1 --arena-map 2 --arena-rounds 3
```

Naming any `--arena-*` option implies `--arena`. Indices wrap, so an
out-of-range machine or arena picks a real one rather than failing. Escape
returns to the main menu.

Arena mode needs no retail assets beyond whatever the rest of the game needs to
boot: every mech, every arena and the HUD font are generated at runtime.

## Controls

The arcade layout this borrows from is played on two sticks with a trigger on
each, and the pad mapping keeps that shape.

| Action                | Keyboard           | Pad                |
| --------------------- | ------------------ | ------------------ |
| Walk / strafe         | `W` `A` `S` `D`    | Left stick         |
| Turn                  | `Q` `E`            | Right stick X      |
| Dash                  | `Left Shift`       | B / left shoulder  |
| Jump (hold to thrust) | `Space`            | A                  |
| Crouch                | `Left Ctrl` or `C` | X                  |
| Left weapon           | `J`                | Left trigger       |
| Centre weapon         | `K`                | Both triggers      |
| Right weapon          | `L`                | Right trigger      |
| Cycle target          | `Tab`              | Y / right shoulder |

Both are read every frame, so either works at any time.

## How it plays

- **The lock does the aiming.** Your mech keeps its shoulders square to the
  target on its own; the sticks decide where the feet go. Manual turn rides on
  top for shaking a lock or lining up with nothing locked.
- **Every trigger is four attacks.** Each of the three weapons has a separate
  definition for standing, crouching, dashing and airborne, so the same button
  is a different attack depending on how you are moving. That is the central
  rule of the mode, and it is why weapons are a 3x4 table.
- **Boost is the resource.** Dashing and jump thrust drain it; standing refills
  it slowly and crouching refills it fast. Empty it and the thrusters lock out
  until it climbs back past 30%.
- **Stagger is separate from damage.** Hits accumulate stagger, which bleeds off
  over time; crossing the threshold floors the mech. Getting up is covered by
  invulnerability, so a knockdown cannot be chained.
- **Rounds.** Best of whatever `--arena-rounds` asks for, 90 seconds each,
  decided on remaining armour if the clock runs out.

## Layout

The mode is split so that the half worth testing has no engine dependencies at
all.

| File             | Depends on  | Purpose                                           |
| ---------------- | ----------- | ------------------------------------------------- |
| `mecha_math.c`   | libc        | 14-bit angles, vectors, deterministic RNG         |
| `mecha_types.h`  | --          | every struct and enum in the mode                 |
| `mecha_arena.c`  | libc        | arena geometry and its collision queries          |
| `mecha_defs.c`   | libc        | the roster and the balance constants              |
| `mecha_sim.c`    | libc        | the 60 Hz tick: movement, weapons, damage, rounds |
| `mecha_ai.c`     | libc        | the computer pilot                                |
| `mecha_mesh.c`   | libc        | procedural mech, arena and effect geometry        |
| `mecha_render.c` | SDL, ROLLER | camera, quad submission, HUD                      |
| `mecha_input.c`  | SDL, ROLLER | keyboard and pad to one input struct              |
| `mecha_mode.c`   | SDL, ROLLER | the frontend state and its fixed-step loop        |

Everything above `mecha_render.c` is a pure function of the world struct plus
one `tMechaInput` per mech per tick. `tests/mecha_sim_test.c` runs whole matches
headless on that, including every roster pairing fighting to a result, and
asserts that two runs from the same seed are byte-identical.

```sh
zig build test-mecha-sim
```

`tests/mecha_render_headless_test.c` covers the other half -- the part no unit
test or compile check can speak for. It builds a real `GameRenderer` in software
mode with no GPU device and no window, renders arena frames into an indexed
buffer, and asserts that the floor, the walls, the mechs, the tracers and the
HUD all reach pixels:

```sh
zig build test-mecha-render
```

Given an output directory it also writes the frames out as indexed PNGs, so the
layout can be looked at rather than only asserted about. The palette in those
dumps is a stand-in -- the real one lives in the retail data, which this test
deliberately does without:

```sh
zig build test-mecha-render -Dmecha-frames=/tmp/frames
```

## Rendering notes

- The mode forces `GAME_RENDER_SOFTWARE` on entry and restores the previous mode
  on exit. It is built around that path's behaviour.
- The software rasteriser has no depth buffer, so `mecha_render.c` sorts every
  quad back to front itself and rejects back faces using the normal each quad
  carries. Closed hulls and the arena walls are one-sided; the walls face
  inward, so a camera pushed outside the arena sees through them rather than at
  a wall of flat colour.
- Quads that straddle the near plane have their offending vertices pulled
  forward along an edge rather than being culled, which keeps the floor tile you
  are standing on from smearing across the screen.
- Flat polygons take their colour from the low byte of the surface flags, and
  `POLYFLAT` already routes `SURFACE_FLAG_TRANSPARENT` to `shadow_poly`, so mech
  shadows and ground dust come free.
- Palette indices are tuned numbers against the stock palette. Every colour the
  mode paints with is named at the top of `mecha_arena.c`, `mecha_defs.c`,
  `mecha_mesh.c` or `mecha_render.c`, so retuning against a different palette
  stays a small edit.

## Known rough edges

- At point-blank range the player's own machine overlaps the target on screen.
  The chase camera centres the lock and drops your mech into the foreground,
  which holds up at normal fighting distance, but two mechs in melee are simply
  in the same place. This wants tuning against real play rather than against a
  still frame.
- The palette indices are tuned by eye, not derived. They are named constants at
  the top of `mecha_arena.c`, `mecha_defs.c`, `mecha_mesh.c` and
  `mecha_render.c` precisely so a retune stays a small edit.
- Effects are opaque flat quads. An indexed frame buffer has no alpha, so a
  blast reads as a coloured burst rather than a fireball; shadows and ground
  dust get real translucency only because `POLYFLAT` routes
  `SURFACE_FLAG_TRANSPARENT` through `shadow_poly`.

## Adding a machine

Append an entry to `s_aMechDefs` in `mecha_defs.c`. The roster test checks that
all twelve weapon entries are filled in, that dashing beats walking, and that
crouching refills boost faster than standing, so a half-finished machine fails
the build rather than shipping a trigger that silently does nothing in one
stance.
