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
zig build run -- --arena --arena-skill 0
```

Naming any `--arena-*` option implies `--arena`. Indices wrap, so an
out-of-range machine or arena picks a real one rather than failing.

`--arena-skill` takes 0 (rookie), 1 (veteran, the default) or 2 (ace).

## The briefing

`--arena` opens on a briefing screen rather than dropping straight into a
fight. It lists the controls, and its rows set up the match:

| Row              | Does                                                |
| ---------------- | --------------------------------------------------- |
| Start match      | Begins the fight                                     |
| Your mech        | Left / right cycles the roster                       |
| Opponent         | Left / right cycles the roster                       |
| Arena            | Left / right cycles the arenas                       |
| Opponent skill   | Rookie, veteran or ace                               |
| Exit to Whiplash | Leaves the arena for the main menu and the race game |

`W`/`S` or up/down move between rows, `A`/`D` or left/right change a setting,
and `Enter` or `Space` selects. A pad works throughout: d-pad or left stick to
move, A to select, B to leave.

Escape leaves a match and returns here; escape on the briefing itself leaves
for the main menu. Every match returns here when it is decided, after holding
on the result for a couple of seconds, so the settings can be changed and
another fought without restarting.

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
| Guard                 | `Left Ctrl` or `C` | X                  |
| Jump cancel           | Guard while airborne | Guard while airborne |
| Left weapon           | `J`                | Left trigger       |
| Centre weapon         | `K`                | Both triggers      |
| Right weapon          | `L`                | Right trigger      |
| Cycle target          | `Tab`              | Y / right shoulder |
| Leave the match       | `Escape`           | --                 |

Both are read every frame, so either works at any time.

## How it plays

- **You point the machine; the lock aims the guns.** The auto-turn only
  squares the shoulders up inside about 34 metres, where a melee exchange is
  too fast to aim by hand. Past that, pointing it is yours to do -- and since
  the lock only holds while the enemy is inside your cone, keeping one at
  range is now something you do rather than something that happens. Aim
  elevation still comes off the lock at any distance; that tilts the guns,
  not the machine.
- **The lock does the aiming, while you have it.** Your mech keeps its
  shoulders square to the target on its own; the sticks decide where the feet
  go. Manual turn rides on top for shaking a lock or lining one up again.
- **The lock is breakable, and that is the central rule.** It holds while the
  target is inside a 32-degree cone of your own heading and drops once it has
  been outside for a third of a second. With it gone the mech stops turning
  itself, weapons fire straight down the barrel with no lead, and missiles
  launch unguided -- so losing it costs accuracy, not just the reticle. The
  brackets close up and go orange when it is live, open and go amber as it
  slips, and sit wide and grey once it has gone.
- **Getting a lock back is harder than keeping one.** On its own it returns
  only when the target is well inside a 12-degree cone. Boosting or jumping
  snaps it on from any angle, which is the fast way back and the reason to
  spend gauge on a dash you did not need for the distance.
- **A boost is a committed line.** The direction is latched when it starts and
  runs until the burst or the gauge does. You can fire the whole way through
  without cutting it short, and the lock is live for all of it.
- **Every trigger is four attacks.** Each of the three weapons has a separate
  definition for standing, crouching, dashing and airborne, so the same button
  is a different attack depending on how you are moving. That is the central
  rule of the mode, and it is why weapons are a 3x4 table.
- **Boost is the resource.** Dashing and jump thrust drain it; standing refills
  it slowly and guarding refills it fast. Empty it and the thrusters lock out
  until it climbs back past 30%.
- **Guard answers a blade.** It is the old crouch: fastest refill, its own row
  of weapons. On top of that a guarding mech takes 15% of a melee hit and half
  the stagger that comes with it. Only melee -- standing in guard against
  gunfire loses, which is what stops it being the only thing anyone does.
- **Jump, then cancel.** Leaving the ground snaps the lock on whatever you
  were pointed at, so going up is the reliable way to find someone who has got
  behind you. Guard in the air then throws the rest of the arc away and drops
  the mech straight down; the landing is shortened and leaves the turn rate
  off its leash for about half a second, long enough to come down facing the
  other way. Up to find them, down to face them.
- **Stagger is separate from damage.** Hits accumulate stagger, which bleeds off
  over time; crossing the threshold floors the mech. Getting up is covered by
  invulnerability, so a knockdown cannot be chained.
- **Rounds.** Best of whatever `--arena-rounds` asks for, 90 seconds each,
  decided on remaining armour if the clock runs out.
- **The computer pilot plays by the same lock rules.** It loses tracking on
  the same cone you do, reaches for the manual stick when the auto-turn stops
  following, and boosts to snap a lost lock back on. Across the roster it
  spends between eight and twenty per cent of a fight without a lock, and the
  test suite asserts both that it loses one and that it mostly holds one.
- **The opponent's skill is mostly its aim.** Every weapon aims itself at
  whatever is locked, so the computer pilot fires a perfect solution unless it
  is given an angular error to miss by. That error is what the three levels
  really set: measured over twelve duels the pilot lands about a third less
  damage at rookie than at ace, and absorbs about a third more. Its reaction
  time to incoming fire varies too, but that turned out not to change the
  outcome measurably -- a pilot that answers every shot instantly also dashes
  constantly, which swings it off its own firing cone and drains the boost it
  needs to dodge with. The comment above `s_aAiProfiles` in `mecha_ai.c` has
  the figures.

- **Machines read by silhouette.** Each carries build multipliers for
  shoulder, torso, limb, head and gun, so a siege platform is wide and
  thick-limbed with an oversized gun on each arm while an interceptor is a
  narrow torso on thin legs. Measured off the mesh as width over standing
  height they span 0.46 to 1.11.
- **There are clouds.** Thirty puffs on a dome around the arena's centre,
  each one tangent to it so it faces the middle -- where the camera is, near
  enough -- rather than being a camera-facing billboard, because a billboard
  high overhead turns edge-on to a camera underneath it and the sky develops
  holes. They are the sky's own five frames from `gentex.drh`, drawn only
  when that bank is there: a cloud that falls back to a flat square is a grey
  slab hanging in the air, which is worse than no cloud. Placement is a hash
  of the cloud's index and the match seed, so nothing is stored between
  frames and the sky does not depend on how many shots have been fired under
  it, and a squared draw crowds them down towards the horizon where they do
  the most work. The whole dome turns about once an hour.
- **The sky is the game's own.** `DrawHorizon` paints it: flat blue above a
  line through the projection, a haze colour below, exactly as it does for
  the race. It reads the camera out of globals that `game_render_set_camera`
  and `set_projection` have already written, so the mode supplies only the
  elevation, the tilt and the ground colour -- and that colour is track chunk
  data, so the arena lends the engine one `HorizonColour` entry for the
  length of the call and puts it back. The nine-band sunset gradient this
  replaced was the mode inventing a sky the engine already had.
- **The machines are jointed.** Legs have knees that bend the way a person's
  do -- the thigh's pose pitch is negated and the knee's is not, and getting
  those two signs the same makes the machine bird-legged, which is a fine
  thing for a mech to be but not what this roster is. The rest of the leg: the thigh swings as a sine
  of the step phase, the knee bends through the forward half of that swing
  and straightens for the half the foot is planted and pushing back, and the
  ankle keeps the sole flat to the floor whatever the leg above it is doing.
  The body is then dropped onto whichever foot reaches lowest, which is what
  makes a stride bob and a guard sink rather than either being animated as
  such -- guard bends the knees now instead of squashing the whole machine
  down to two thirds of its height, and on a knee that bends backwards it
  has to squat deep to lower anything at all, because the knee travels
  forward as far as the hip drops and the two cosines all but cancel until
  the angles get large.
- **The legs are not the machine.** They follow the line of travel while the
  torso holds the aim, so a mech strafing across your guns walks sideways
  with its shoulders square to you. A heading more than a quarter turn off
  the shoulders means it is walking backwards, so the cycle runs in reverse
  rather than the machine spinning round; what is left is clamped, because
  feet pointed further off the shoulders than that are not strafing, they
  are tangled. `iLegYaw` is the one piece of animation state the simulation
  owns, because it is smoothed over time and the mesh is rebuilt every frame.
- **Arms have elbows and they aim.** Each arm is a shoulder, an elbow and
  the gun on the end of the forearm, and the whole chain turns and elevates
  onto the line the weapons are pointing down -- which under a held lock is
  the line to the target, so the guns track an enemy circling you. Firing
  kicks the arm that fired. The head looks the same way on a shorter leash:
  an arm is a gun mount, a neck is a neck.
- **The camera chases you, not the enemy.** Beyond knife range it sits behind
  your machine and looks where your machine is looking, so the view is steady
  while you steer. Inside `MECHA_CLOSE_QUARTERS` it swings onto the lock and
  centres the target instead, which is where an exchange is too fast to frame
  by hand.
- **Firing off a boost brings you back onto the lock.** A shot fired out of a
  dash, out of a jump, or through a cancel snaps the machine onto its target
  for half a second. A shot fired walking or standing does not: those are the
  states where the heading is yours, and reclaiming it on every trigger pull
  would be the old auto-turn under another name.
- **Shots are plasma.** The frames are the retail sky's own cloud puffs --
  `gentex.drh` has no bolt art, and at bolt size a soft blue puff reads as
  plasma. Beams keep their coloured streak and gain a boiling head; homing pods and lobbed charges are the sprite outright. The frames
  cycle on a fixed cadence rather than over a lifetime, so a bolt that lives
  for a fifth of a second and one that arcs for two shimmer at the same rate.
  Muzzle flashes come off the same sequence, hits walk the blast frames, and
  thruster plumes walk the flame frames. Solid rounds stay solid -- a slug is
  not made of light -- and the streak keeps the weapon's own colour, because a
  textured quad draws the frame's colours and nothing else: skinning the
  streak would make every machine's fire the same blue.
- **The HUD face is missing glyphs, so the mode fills them in.** The retail
  restricted font has no full stop, and a name like SJ Mk.IV would come out
  with a four pixel hole in it. Text is printed a character at a time and
  anything the face lacks is drawn from the mode's own glyphs, at the pen
  position the retail advance table says it occupies and in the retail
  face's colour, so the two stay in step across a string.
- **Blasts throw debris.** A kill spawns a short flash plus a burst of
  particles that fly out, fall under gravity, shrink, and cool down a warm
  palette ramp.
- **Effects use the game's own frames when they are there.** `gentex.drh`
  carries the retail explosion, flame and smoke animations as 64x64 tiles,
  and the engine already decompresses that bank and uploads it as an atlas.
  The mode checks the file exists, lets the existing loader do the work, and
  points its effect quads at the right frames. They are drawn masked, with
  palette index 0 skipped rather than written -- every frame in that bank
  sits on index 0, between a third and nine tenths of each tile, so drawing
  them opaque would put a black square round every explosion. With no retail
  data it draws the flat-shaded particles instead and everything still runs
  -- the file check is what makes that true, because the stock loader exits
  the process on a missing bank rather than returning a failure.

- **Surfaces use the game's own textures.** Ground, walls and cover are
  drawn from the retail texture banks when they are installed -- track1.drh
  for ground and walls, building.drh for the faces of cover -- through the
  loaders the engine already has. Each arena takes a different surface, and
  every surface keeps a palette index so a checkout with no retail data
  still comes up, flat-shaded, exactly as before.
- **The palette matters more than it looks.** With the retail data present
  the mode now loads palette.pal rather than its own fallback table. It has
  to: retail tiles are drawn in the retail palette's indices, so resolving
  them through a table that defines thirty colours turns tarmac into static.

- **Two retail fonts, not one.** minitext.bm is the restricted set the race
  HUD labels driver names and speed with, and it is what the small rows use.
  The title and the round banners use font6.bm, the larger sprite face the
  game announces things in. That one takes a colour, so a banner can still
  go green for a win and red for a loss.

- **Machines carry their weight.** Velocity is driven, not assigned. The
  race game's cars work the same way: a grip figure limits how fast sideways
  motion is corrected, whatever is left over bleeds off on its own, and a
  car cannot simply be told to be going somewhere else. Each machine has a
  grip, a drive acceleration and a brake, all in absolute metres per second
  squared. A siege platform sheds its old direction in about fourteen ticks
  and takes nineteen to reverse; an interceptor does both in four and seven.

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
  Inside knife range the camera centres the lock and drops your mech into the
  foreground, and two mechs in melee are simply in the same place. This wants tuning against real play rather than against a
  still frame.
- The clouds are the arena's own dome, not the retail one. `DrawHorizon`
  ends by drawing its dome, but that is forty quads ten million units out in
  a Z-up coordinate system, submitted through the renderer's cloud
  subdivision path; handing that an arena camera makes a frame take minutes
  instead of milliseconds, so the arena disables them at the `textures_off`
  bit while it calls in and hangs its own dome as ordinary arena geometry
  instead. Same five frames, same look, sorted and drawn like everything
  else in the scene.
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
