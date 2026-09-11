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

`--arena` opens on a briefing screen rather than dropping straight into a fight.
Its rows set up the match:

| Row              | Does                                                 |
| ---------------- | ---------------------------------------------------- |
| Start match      | Begins the fight                                     |
| Your mech        | Left / right cycles the roster                       |
| Opponent         | Left / right cycles the roster                       |
| Arena            | Left / right cycles the arenas                       |
| Opponent skill   | Rookie, veteran or ace                               |
| Round time       | 30, 60, 90, 120 seconds, or deathmatch               |
| Enemy weapons    | Live, or held -- a debug switch, see below           |
| View controls    | Opens the controls on a page of their own            |
| Exit to Whiplash | Leaves the arena for the main menu and the race game |

**Round time** sets the clock for every round. Deathmatch switches it off rather
than setting it very high, which is a different game and not a longer one: with
a clock, a round nobody wins is decided on whoever has the most armour left, and
a deathmatch round simply runs until somebody falls over. The clock in the
corner of the HUD reads `--` when there is none.

**Enemy weapons** is a debug switch and says so on the row. Held, the computer
pilots go on closing, circling, boosting and dodging exactly as they would --
they simply never pull a trigger, which is what makes it useful for looking at
the movement rather than a difficulty setting that makes them passive.

`W`/`S` or up/down move between rows, `A`/`D` or left/right change a setting,
and `Enter` or `Space` selects. A pad works throughout: d-pad or left stick to
move, A to select, B to leave.

Escape leaves a match and returns here; escape on the briefing itself leaves for
the main menu. Every match returns here when it is decided, after holding on the
result for a couple of seconds, so the settings can be changed and another
fought without restarting.

Arena mode needs no retail assets beyond whatever the rest of the game needs to
boot: every mech, every arena and the HUD font are generated at runtime.

## Controls

Also on their own page in game, off the briefing's **View controls** row. They
used to be printed down the middle of the briefing, where they were ten lines a
returning player had already read and the setup rows had nowhere to grow past.

The arcade layout this borrows from is played on two sticks with a trigger on
each, and the pad mapping keeps that shape.

| Action                | Keyboard             | Pad                  |
| --------------------- | -------------------- | -------------------- |
| Walk / strafe         | `W` `A` `S` `D`      | Left stick           |
| Turn                  | `Q` `E`              | Right stick X        |
| Dash                  | `Left Shift`         | B / left shoulder    |
| Jump (hold to thrust) | `Space`              | A                    |
| Guard                 | `Left Ctrl` or `C`   | X                    |
| Jump cancel           | Guard while airborne | Guard while airborne |
| Left weapon           | `J`                  | Left trigger         |
| Centre weapon         | `K`                  | Both triggers        |
| Right weapon          | `L`                  | Right trigger        |
| Cycle target          | `Tab`                | Y / right shoulder   |
| Leave the match       | `Escape`             | --                   |

Both are read every frame, so either works at any time.

On the ZIZIN KLR 330 the same two buttons drive it: **dash is the accelerator
and guard is the brake**, the stick and the turn axis both steer, and jump does
nothing because it has no legs to jump with. The stick answers the throttle as
well, forward and back, because a car nobody can drive with the same keys they
walk everything else with is a car nobody drives. Its three triggers are three
loads for one gun out of one magazine of nine -- buckshot left, the rifle in the
centre, a lobbed shell on the right -- so brake into the corner, spin it round
on the stick, and pick what to spend the next round on.

## How it plays

- **You point the machine; the lock aims the guns.** The auto-turn only squares
  the shoulders up inside about 34 metres, where a melee exchange is too fast to
  aim by hand. Past that, pointing it is yours to do -- and since the lock only
  holds while the enemy is inside your cone, keeping one at range is now
  something you do rather than something that happens. Aim elevation still comes
  off the lock at any distance; that tilts the guns, not the machine.

- **The lock does the aiming, while you have it.** Your mech keeps its shoulders
  square to the target on its own; the sticks decide where the feet go. Manual
  turn rides on top for shaking a lock or lining one up again.

- **The lock is breakable, and that is the central rule.** It holds while the
  target is inside a 32-degree cone of your own heading and drops once it has
  been outside for a third of a second. With it gone the mech stops turning
  itself, weapons fire straight down the barrel with no lead, and missiles
  launch unguided -- so losing it costs accuracy, not just the reticle. The
  brackets close up and go orange when it is live, open and go amber as it
  slips, and sit wide and grey once it has gone.

- **Getting a lock back is harder than keeping one.** On its own it returns only
  when the target is well inside a 12-degree cone. Boosting or jumping snaps it
  on from any angle, which is the fast way back and the reason to spend gauge on
  a dash you did not need for the distance.

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

- **A cancel drops like a stone.** 120 metres a second, not the 46 it was: the
  machine is simply on the ground, inside a fifth of a second from any height it
  can reach. That is what makes the cancel a way out of an arc rather than a
  slightly faster way of finishing one.

- **Jump, then cancel.** Leaving the ground snaps the lock on whatever you were
  pointed at, so going up is the reliable way to find someone who has got behind
  you. Guard in the air then throws the rest of the arc away and drops the mech
  straight down; the landing is shortened and leaves the turn rate off its leash
  for about half a second, long enough to come down facing the other way. Up to
  find them, down to face them.

- **Stagger is separate from damage.** Hits accumulate stagger, which bleeds off
  over time; crossing the threshold floors the mech. Getting up is covered by
  invulnerability, so a knockdown cannot be chained.

- **Rounds.** Best of whatever `--arena-rounds` asks for, 90 seconds each by
  default and settable on the briefing, decided on remaining armour if the clock
  runs out -- or on a deathmatch, not decided until somebody is down.

- **Five arenas, and two of them are terrain.** Three are walled boxes with
  cover in them. COLDWATER MEADOW is an octagon of open country half a kilometre
  across -- seven truncated cones of hill, trees and rocks to fight around, and
  no buildings and no walls. TOWER SEVEN ROOF is a square with no walls either
  -- walk off it and you are falling -- with a raised hexagonal tabletop in the
  middle of it and a block in each corner.

- **The tabletop is answered, not baked.** The hills go into the terrain grid
  because they are meant to be lumpy: a hill built out of grid corners is a
  dozen facets, which is what a hill should look like. A made thing with six
  straight edges is not, so the mesa is a function of position that the height
  query adds on top -- and the ground mesh picks it up for free, because the
  mesh samples that same query at every corner it draws. It reaches its apothem
  across a face and two-over-root-three of it towards a corner, which is what
  makes it a hexagon rather than a circle, and its sides are a walkable ramp
  rather than a wall.

- **The meadow has no wall, and the forest does the work.** The boundary is
  still there and still stops a machine; what is missing is anything drawn on
  it. Past it the ground runs on to more than twice the arena again, as rings of
  the boundary's own shape rather than a grid with the middle knocked out -- a
  grid coarse enough to be cheap drops a wedge of ground for every tile that
  overlaps the arena, and the horizon comes out full of holes. Standing on that
  ground are a few hundred of the game's own tree sprites, billboarded,
  non-collidable and crowded in against the boundary by a squared draw so the
  edge of the fight reads as the edge of a clearing. None of them are inside: a
  tree with no collision standing where the fight is would be a tree machines
  walk through.

- **The ground is drawn and shaped at different resolutions, and both are
  per-arena.** The meadow is twice the size of the others, so it takes both a
  finer terrain grid -- or its hills round off into bumps, and a bump is not a
  ramp -- and more floor tiles, or every one of them comes out stretched over
  forty metres of grass.

- **The ground is the race game's ground.** Every cell of an arena carries the
  engine's own surface flags, and the two that matter are the pit
  (`SURFACE_FLAG_PIT` with `SURFACE_FLAG_SKIP_RENDER`, exactly how the race game
  builds one: a surface that still answers height queries, is simply not drawn,
  and is fatal to stand on) and the magnet, below. Nothing in the roster uses a
  pit at the moment -- the roof had one and it did not work, and a tabletop you
  take is a better fight than a hole you avoid -- but the machinery is there and
  tested. Falling off the world past an arena's kill plane still costs the
  machine everything it has left.

- **A roof needs a tower under it.** An open arena draws its own edge downwards,
  and how far is the difference between the top of a building and a table
  standing in the sky. The roof's runs a hundred and fifty metres, panelled
  coarsely because nothing that far below the player is being looked at closely.

- **A boost up a slope launches you.** Ground flagged
  `SURFACE_FLAG_NON_MAGNETIC` does not hold a machine down, which is the rule
  `control.c` applies to the cars: the rate the ground rose underneath you is
  real upward velocity, and at the crest you keep it. The meadow's hills are all
  non-magnetic, so walking over one hops the top for a few ticks and boosting up
  the same slope leaves it for the better part of a second and a half. Their
  tops are flat rather than pointed because a cone with a peak on it throws a
  walking machine into the air at the apex.

- **The computer pilot watches its feet.** It does not path around anything; it
  declines to walk into it. Three separate distances: a stride plus what it is
  carrying on foot, the whole length of a burst before it presses boost, and
  only as far as a cancel needs once the burst is running -- looking further
  than that has a pilot flinching at an edge it was always going to stop short
  of, and a panicked counter-burst is its own way off a roof. When the way ahead
  is nothing it turns away by the smallest angle that finds ground again,
  because on a roof with a hole in the middle straight back the way you came is
  as likely to be the pit as the edge was. A burst it wants to throw away is
  cancelled the way a player cancels one, by letting the button up and pressing
  it again against the stick: leaning on boost cancels nothing. Six one-minute
  fights on the roof, and it walks off it zero times; being shot off it still
  counts as a fair way to lose.

- **The computer pilot plays by the same lock rules.** It loses tracking on the
  same cone you do, reaches for the manual stick when the auto-turn stops
  following, and boosts to snap a lost lock back on. Across the roster it spends
  between eight and twenty per cent of a fight without a lock, and the test
  suite asserts both that it loses one and that it mostly holds one.

- **The opponent's skill is mostly its aim.** Every weapon aims itself at
  whatever is locked, so the computer pilot fires a perfect solution unless it
  is given an angular error to miss by. That error is what the three levels
  really set: measured over twelve duels the pilot lands about a third less
  damage at rookie than at ace, and absorbs about a third more. Its reaction
  time to incoming fire varies too, but that turned out not to change the
  outcome measurably -- a pilot that answers every shot instantly also dashes
  constantly, which swings it off its own firing cone and drains the boost it
  needs to dodge with. The comment above `s_aAiProfiles` in `mecha_ai.c` has the
  figures.

- **Standing is a pose, not a default.** A machine with nobody to fight stands
  with its feet apart and its knees off the lock; give it a lock to hold and it
  settles into the fight -- lower, wider, one foot forward. Neither is animated
  as such: the two are the ends of one blend that `tMechaMech::fCombat` drives,
  and the only movement in either is a slow rock of the weight from one foot to
  the other. Measured off the mesh, the fighting stance stands five per cent
  lower and thirty per cent wider than the neutral one.

- **Guns come up fast and go down slowly.** An arm that is neither locked on nor
  shooting unfolds and hangs: the shoulder stops tracking, the elbow gives up
  its right angle, and the gun ends up beside the machine's own knee -- half the
  height and a sixth of the reach it has when levelled at you. It is the
  clearest read in the game for which of two machines across the arena is about
  to shoot. Raising takes about an eighth of a second and lowering the better
  part of one, because a machine that has just been shot at must not spend half
  a second getting ready and a machine that has merely lost sight of someone
  must not drop its guard the instant the lock breaks.

- **Boosting on the ground is a glide, not a run.** The thrusters are doing the
  work, so the legs are not driving the machine anywhere -- they hold it up and
  steer it, which is a skater's problem and not a runner's. Both knees stay
  bent, the weight stays low, and one leg at a time reaches out to the side and
  back in a long push while the other glides underneath. Measured against the
  walk it is a fifth narrower fore and aft and half again as wide across, which
  is the difference between feet that pass each other and feet that go out to
  the side.

- **Both feet stay on the floor, and the hips are what pay for it.** A stance
  and a glide are poses the machine holds rather than cycles it steps through,
  so neither may leave a foot hanging in the air. The leg that reaches further
  takes the difference out in hip roll -- it splays until its foot is back down
  -- which is not a fudge but the shape itself: a skater at full stretch has its
  pushing leg out to the side precisely because that leg is straight. The roll
  is a frame of its own above the thigh, because rolled-then-swung puts the foot
  exactly `cos(roll)` of the way down and swung-then-rolled does not, and the
  ankle takes the same roll back so the sole stays flat instead of driving its
  inner corner through the ground.

- **Joints stand proud of the limbs they join.** A knee the same width as the
  shin below it has coplanar side faces with it the moment the joint passes
  through straight, and with no depth buffer there is nothing to sort that out;
  a knee that bulges is both the fix and what the reference art draws anyway.

- **One of them is a car.** The ZIZIN KLR 330 is the race game's own Zizin with
  a handgun the size of itself floating off the front right wheel, and it plays
  by different rules on purpose. No boost, no jump, no strafe, no gauge to
  spend: one signed number of speed along its own nose, boost as the accelerator
  and guard as the brake, and steering rather than turning. It is a sixth of a
  machine's height and roughly two thirds its armour, so it is hard to hit and
  does not survive being hit.

- **Its steering is the race game's steering.** Whiplash works the lock out as
  `input * (1 + (360 - speed) / 60)` and then throws it away entirely below the
  car's own steering speed limit. Both halves are here: the lock is widest just
  off a standstill and narrows as the speed comes up, and a car that is not
  moving cannot be pointed at all. That second rule is the whole of how this
  machine fights, because it also has no auto-turn at any range -- the only way
  it holds a lock, or lines up its gun, is to drive at somebody and keep them in
  the middle of the screen.

  360 is that game's reference speed, so the bonus runs from **seven times** the
  input at a standstill to nothing at all flat out: 57 degrees a second on the
  stick, times seven when crawling. Flat out the car barely turns and has to be
  slowed into a corner rather than steered round one.

- **And nothing limits how far it comes round.** Whiplash simply accumulates the
  yaw; there is no ceiling on it anywhere, which is why a car can be spun
  through a whole circle on the stick. What stopped that here was not a clamp
  but a sign test: the steering flipped direction the moment the velocity fell
  more than a quarter turn behind the nose, which is the middle of every drift,
  so the stick fought the slide exactly when it should have been driving it.

  Whiplash decides that on `fFinalSpeed`, the car's own signed speed along its
  nose, and on a track that is the only speed it has -- position is advanced
  straight along the heading, so a Whiplash car cannot travel at an angle to
  where it points at all. This one carries a real velocity vector, so the test
  is on the car's reverse speed instead, which is a third of its forward one and
  nothing like drifting pace. Braking into full lock now takes it **364 degrees
  round in two seconds with 180 degrees of slip**.

- **It sits on the ground, not above it.** A car sitting perfectly flat while it
  drives up the side of a hill is what gives away that the hill is a height
  field rather than a surface. So the machine asks what the ground is doing
  across its own footprint -- fore against aft for the climb, left against right
  for the traverse, both measured against its own collision radius -- and takes
  the slope's own angle. A one-in-three grade comes out as eighteen degrees,
  because that is what a one-in-three grade is. It eases rather than snaps, so a
  gradient changing between grid cells reads as suspension rather than as a
  glitch.

  Only on wheels: a walking machine has feet and a gait to put them down with,
  and tilting the whole of it to match the ground would fight both. And only on
  the terrain -- up on the roof of a box the height field underneath is
  describing ground the machine is nowhere near, and following it would lean the
  car over on a flat roof.

- **Floored, it goes over on its roof.** Something tall enough to have a face
  pitches forward onto it. A thing nine metres long and two high has nowhere to
  pitch to, and a Zizin standing on its nose reads as a glitch rather than as a
  wreck, so its knockdown is half a roll instead. The pose turns about the car's
  own floor, so the origin is raised by however far the lowest corner has gone
  under -- otherwise half a roll buries the whole body in the tarmac.

- **One gun, three loads, nine rounds between them.** It carries all three
  weapon slots so it reads and plays like everything else on the roster, but
  they are three things to put through the same gun, not three guns -- a
  magazine of nine that every trigger draws from and one long reload when it
  runs dry. Every round spent is spent out of all three, because a magazine that
  could be stretched by rolling across the other two triggers would not be a
  magazine. So the choice is never which gun to use, it is what to spend the
  next round on.

  | Trigger | Load             | What it is for                                                                                                      |
  | ------- | ---------------- | ------------------------------------------------------------------------------------------------------------------- |
  | LW      | **KLR BUCKSHOT** | Seven pellets across five degrees, gone in half a second. Devastating at ramming distance and litter at any other.  |
  | CW      | **KLR LANCE**    | One round, no spread, 520 m/s, and a long look down the barrel afterwards. The shot the machine is built around.    |
  | RW      | **KLR MORTAR**   | A shell lobbed over whatever is in the way, with twelve metres of blast. Slow enough to dodge if it is seen coming. |

  And it shoves the car -- the recoil is a real push in the simulation, not a
  drawing, which is the other half of what makes the reload bearable: it buys
  distance.

- **The computer pilot had to learn what a spread is worth.** Scoring a
  scattergun as though every pellet lands at any range made the car fire
  buckshot across the whole arena and never once reach for its rifle: seven
  pellets of twenty-four counted as a hundred and sixty-eight whether the target
  was fifteen metres away or a hundred and fifty. What is scored now is the
  share of the cone the target still covers at that distance, which puts all
  three loads in use and costs the roster nothing measurable -- Kira, the only
  other machine with a real spread, fights about as well either way, it just
  reaches for its sabre sooner.

- **With no melee row, its close-quarters answer is the bumper.** Running
  somebody over is charged on the speed the gap is closing at rather than on its
  own speed, so driving alongside is not a ram and a head-on is worse than
  catching them up, and there is a cooldown on it because a car resting against
  somebody is not running them over sixty times a second.

- **The mode's quads reach POLYTEX reversed.** POLYTEX works its own texture
  coordinates out from the projected polygon, so the order the four corners
  arrive in is what decides how the tile lies on them -- and the arena winds its
  quads the other way round their faces from the way the race game winds the
  geometry in its own data files. Nothing else ever noticed: this renderer
  rejects back faces off the stored normal rather than off the projected
  winding, so a quad wound backwards still culls, sorts and fills correctly, and
  every texture the mode had worn until the car turned up -- grass, tarmac,
  concrete, a plasma bolt -- was near enough symmetrical to look right mirrored.
  Put lettering on one and it reads backwards.

- **And the car is reflected into this frame, not just rotated.** The race
  game's frame is right-handed -- x along the car, y across it, z up -- and this
  one is not: x across, y up, z forward. Swapping the three axes alone builds
  the car's mirror image, with its wheel arches, its exhausts and both flanks of
  its livery on the wrong sides, so the lateral axis is negated.

- **Which is why its artwork is the exception to the rule above.** Reflecting
  the body reverses every winding the plan had, so its panels reach POLYTEX
  already turned round once and all face inwards in this frame. Turning them
  round again, the way the mode's own quads need, is what put ZIZIN on the car
  as NIZIZ; a quad carrying retail artwork says so with a flag and keeps the
  order it arrived in. Both halves have to be right together, which is what made
  them hard to separate: fix either one alone and the car is either mirrored
  with readable paint or the right way round with the paint backwards.

- **Its body is the game's own car, paint and all.** `xzizin_coords` and
  `xzizin_pols` out of `carplans.c` -- the same fifty quads the Zizin is drawn
  with on the track -- with the axes swapped from the race game's (x along, y
  across, z up) into the arena's, and `xzizin.bm` loaded into a car texture slot
  to paint them. Each panel is resolved the way the race game's own draw path
  resolves it: most carry a texture word with the tile in the low byte; eight
  reach theirs through the car's animation table, which is where the wheels and
  the livery live; nine carry no texture flag at all and are a plain palette
  index, which is how the tyres come out black. Without the retail data the
  whole body falls back to the machine's own two colours, split by which way
  each panel looks. The gun is built here out of the same boxes everything else
  is, and is left unpainted -- it is not part of the car.

- **The chase camera scales with what it is behind.** It was written around a
  fourteen-metre machine, which is what the roster mostly is; a car a sixth of
  that would be a speck under a camera hung fourteen metres over it. Not all the
  way down, though -- a car doing seventy metres a second needs to see further
  ahead of itself than two metres of camera height would give it.

- **Machines read by silhouette.** Each carries build multipliers for shoulder,
  torso, limb, head and gun, so a siege platform is wide and thick-limbed with
  an oversized gun on each arm while an interceptor is a narrow torso on thin
  legs. Measured off the mesh as width over standing height they span 0.46 to
  1.11.

- **The draw order is the depth buffer.** There isn't one, so a quad is drawn
  either wholly before another or wholly after, and the key that decides which
  lives with the geometry in `mecha_quad_depth_key` rather than in the renderer.
  Most quads sort on their middle. Two do not. A shadow lying on the floor sorts
  on its nearest corner and the ground beneath it on its farthest, because a
  tile whose middle falls nearer than the shadow's would otherwise be painted
  over the top of it and cut the shadow in half along an edge that slides as the
  camera moves -- and that holds whichever of the two is larger. Self-lit
  sprites are pulled forward by their own half-width, which is exactly the
  radius of the volume they stand for: everything inside the fireball is
  outranked, everything outside it is not, so a shoulder standing clear of a
  blast still occludes it. The narrower edge is the one measured, so a long
  tracer cannot claim to be half its length nearer than it is.

- **There are clouds.** Thirty puffs on a dome around the arena's centre, each
  one tangent to it so it faces the middle -- where the camera is, near enough
  -- rather than being a camera-facing billboard, because a billboard high
  overhead turns edge-on to a camera underneath it and the sky develops holes.
  They are the sky's own five frames from `gentex.drh`, drawn only when that
  bank is there: a cloud that falls back to a flat square is a grey slab hanging
  in the air, which is worse than no cloud. Placement is a hash of the cloud's
  index and the match seed, so nothing is stored between frames and the sky does
  not depend on how many shots have been fired under it, and a squared draw
  crowds them down towards the horizon where they do the most work. The whole
  dome turns about once an hour.

- **The sky is the game's own.** `DrawHorizon` paints it: flat blue above a line
  through the projection, a haze colour below, exactly as it does for the race.
  It reads the camera out of globals that `game_render_set_camera` and
  `set_projection` have already written, so the mode supplies only the
  elevation, the tilt and the ground colour -- and that colour is track chunk
  data, so the arena lends the engine one `HorizonColour` entry for the length
  of the call and puts it back. The nine-band sunset gradient this replaced was
  the mode inventing a sky the engine already had.

- **The machines move faster than they animate.** Speeds went up by about a
  third across the roster, and a stride is 5.2 metres of ground rather than two:
  tying the cycle tightly to distance turned the extra speed into a sprint of
  little steps, where a longer stride reads as something heavy moving quickly.
  The legs reach to match -- at full stretch there is seven tenths of the
  machine's own standing height between its feet.

- **A boost is a committed act.** The button starts a burst and does not hold it
  up: once it is running, only the clock, an empty gauge, a jump or a wall ends
  it. Letting go does nothing, which is what makes a dash something you spend
  rather than something you steer.

- **But a committed burst can still be turned.** Two ways in, and the difference
  between them is what they cost. Boost again while pushing back against the
  direction you left on and the burst restarts the other way -- that is the
  cancel, and it is why commitment is not a trap. Or let the stick go and tap a
  new direction: the burst turns without a second press, and the release is the
  whole price of it. Either way the clock starts again the new way rather than
  limping out the remainder of the old one -- a crossing step is a dash that
  changed its mind, not the tail of one, and what stops it going on forever is
  the gauge, which drains throughout. Holding a different direction down does
  nothing at all, so leaning on the stick cannot walk a dash round in a circle.

- **The speed outlives the burst.** A dash that ends hands its momentum to a
  coast: the same drive the walk uses with the authority turned down at both
  ends, so the machine bleeds off what it was carrying slowly and slides while
  it does. Measured over a third of a second after a burst ends, a machine
  coasts about ten metres where one that simply stopped walking covers under
  two.

- **And it comes off walls.** The push the arena applies to get a machine back
  out of a wall is the surface normal, which is all a bounce needs. At walking
  pace it just leans on the wall; carrying a boost into the same wall it
  ricochets off at a bit over half the speed it arrived at, the way the race
  game's cars do, and the burst is over -- you hit something.

- **Dashing works in the air.** A burst is flat wherever it starts, so gravity
  waits until it is done: an air dash holds its height, runs the same clock,
  steers and cancels the same way, and drops the machine back into a fall when
  it ends. An arc is now something the other player has to read rather than
  something they can wait out.

- **The machines are jointed.** Legs have knees that bend the way a person's do
  -- the thigh's pose pitch is negated and the knee's is not, and getting those
  two signs the same makes the machine bird-legged, which is a fine thing for a
  mech to be but not what this roster is. The rest of the leg: the thigh swings
  as a sine of the step phase, the knee bends through the forward half of that
  swing and straightens for the half the foot is planted and pushing back, and
  the ankle keeps the sole flat to the floor whatever the leg above it is doing.
  The body is then dropped onto whichever foot reaches lowest, which is what
  makes a stride bob and a guard sink rather than either being animated as such
  -- guard bends the knees now instead of squashing the whole machine down to
  two thirds of its height, and on a knee that bends backwards it has to squat
  deep to lower anything at all, because the knee travels forward as far as the
  hip drops and the two cosines all but cancel until the angles get large.

- **Six gaits, not one.** Standing and gliding are poses the machine holds;
  walking is a cycle it steps through; hanging in the air and driving through it
  are poses with a slow sway in them. Hanging is not a tuck -- a tuck is what
  you do to clear something -- but a machine with its weight off its feet: one
  leg reaching a little, one trailing, both knees soft. An air dash is half of
  each: the lead leg holds a glide's edge because the machine is being driven
  somewhere, the other hangs because there is nothing under either of them to
  push against. Measured pose against pose, the pairings are between one and
  four metres apart at their furthest point, which is what says they are
  actually different shapes rather than the same shape at different speeds.

- **A glide is timed, not paced.** Every other cycle here runs on ground
  covered, which is what makes a heavy machine take slow steps without anything
  having to say so. A boost breaks that: at seventy metres a second a stroke
  every five metres is fourteen cycles a second, and legs moving that fast are a
  grey blur. So the glide runs on its own clock -- one long push every two
  thirds of a second, which is most of what makes it read as gliding rather than
  sprinting.

- **A boost squares the legs to itself.** Strafing is held to 52 degrees off the
  shoulders, because feet pointed further round than that are not strafing. A
  dash is not a strafe -- the machine is being driven bodily one way -- so the
  legs come all the way round to the burst, however far that is, while the
  shoulders and the arms go on holding the lock. Running one way and shooting
  another is the shape of the whole thing.

- **The legs are not the machine.** They follow the line of travel while the
  torso holds the aim, so a mech strafing across your guns walks sideways with
  its shoulders square to you. A heading more than a quarter turn off the
  shoulders means it is walking backwards, so the cycle runs in reverse rather
  than the machine spinning round; what is left is clamped, because feet pointed
  further off the shoulders than that are not strafing, they are tangled.
  `iLegYaw` is the one piece of animation state the simulation owns, because it
  is smoothed over time and the mesh is rebuilt every frame.

- **Arms have elbows and they aim.** Each arm is a shoulder, an elbow and the
  gun on the end of the forearm, and the whole chain turns and elevates onto the
  line the weapons are pointing down -- which under a held lock is the line to
  the target, so the guns track an enemy circling you. Firing kicks the arm that
  fired. The head looks the same way on a shorter leash: an arm is a gun mount,
  a neck is a neck.

- **The camera chases you, not the enemy.** Beyond knife range it sits behind
  your machine and looks where your machine is looking, so the view is steady
  while you steer. Inside `MECHA_CLOSE_QUARTERS` it swings onto the lock and
  centres the target instead, which is where an exchange is too fast to frame by
  hand.

- **And it does not dodge what it cannot see past.** It used to: a segment trace
  to whatever it was looking at, and up to six three-metre steps upward until
  the line came clear. That was always a little eager -- it swung the whole
  arena for one pillar -- and it got a great deal worse once the ground itself
  started blocking that trace, because then every hill you drove behind heaved
  the camera into the air.

  Virtual-On does not move the camera for this at all. It leaves the camera
  where it belongs and turns whatever is in the way transparent, which keeps the
  frame still and tells the player exactly what is happening. That wants a
  renderer that can blend, so it is not written yet; until it is, nothing
  happens, which is better than the wrong thing happening quickly. The floor
  clamp is not this and stays -- keeping the camera out of the ground is not
  occlusion avoidance, it is not being underground.

- **Firing off a boost brings you back onto the lock.** A shot fired out of a
  dash, out of a jump, or through a cancel snaps the machine onto its target for
  half a second. A shot fired walking or standing does not: those are the states
  where the heading is yours, and reclaiming it on every trigger pull would be
  the old auto-turn under another name.

- **Fire is colour coded.** The game's plasma frames are blue and there is only
  one set of them, so every machine's fire came out the same colour and a
  crossfire was unreadable. The render layer builds recoloured copies of the
  bank at run time -- walking each frame's palette indices onto the nearest
  colour the palette has in the wanted hue at the same brightness, keeping index
  0 as index 0 because that is the transparent key -- and uploads them as banks
  of its own in the engine's spare texture slots. Hue is weighted over
  brightness in that search, or a wanted violet comes back as a grey of about
  the right weight, grey being near everything. Warm, violet and green, with
  cyan and white left on the original blue. Without the data or the palette none
  are built and every shot is blue again, which is duller and not broken.

- **Shots settle it between themselves.** Two shots that meet are worth what
  they do: within a sixth of each other they trade and both are gone, and
  outside that the heavier one carries on through unchanged. Anything carrying a
  blast goes off where it was stopped rather than blinking out, so shooting a
  bomb down is a decision about where it explodes rather than whether it does.
  Two shots from the same machine ignore each other; a laid mine and a swing
  carried in front of a machine are not things in flight and take no part.

- **A bomb leaves a fireball standing.** The blast pays out its damage as it
  always did, and then a sphere is left behind for about four tenths of a
  second: it opens from a third of the blast radius to all of it, burns anyone
  who walks in afterwards -- once each, with whoever was caught by the blast
  itself marked as already burned -- and eats anything shot through it. It
  cannot be shot down. Drawn as puffs on its surface rather than as one
  billboard, because what has to read is where its edge is.

- **Shots are plasma.** The frames are the retail sky's own cloud puffs --
  `gentex.drh` has no bolt art, and at bolt size a soft blue puff reads as
  plasma. Beams keep their coloured streak and gain a boiling head; homing pods
  and lobbed charges are the sprite outright. The frames cycle on a fixed
  cadence rather than over a lifetime, so a bolt that lives for a fifth of a
  second and one that arcs for two shimmer at the same rate. Muzzle flashes come
  off the same sequence, hits walk the blast frames, and thruster plumes walk
  the flame frames. Solid rounds stay solid -- a slug is not made of light --
  and the streak keeps the weapon's own colour, because a textured quad draws
  the frame's colours and nothing else: skinning the streak would make every
  machine's fire the same blue.

- **The HUD face is missing glyphs, so the mode fills them in.** The retail
  restricted font has no full stop, and a name like SJ Mk.IV would come out with
  a four pixel hole in it. Text is printed a character at a time and anything
  the face lacks is drawn from the mode's own glyphs, at the pen position the
  retail advance table says it occupies and in the retail face's colour, so the
  two stay in step across a string.

- **A landing throws a ring of dust.** Seven flat puffs of the smoke sequence
  sliding outwards along the ground from the feet, spaced evenly and then
  jittered off the spokes so a touchdown does not read as a cog. It was one
  expanding square before, which read as a stain spreading rather than as
  anything being kicked up. They are decals, sorted on top of the floor like a
  shadow but filled like a sprite -- that is what `MECHA_QUAD_DECAL` is for, as
  against `MECHA_QUAD_SHADOW`, which also says how the quad is filled. With no
  sprite bank the old single stain is still there.

- **Blasts throw debris.** A kill spawns a short flash plus a burst of particles
  that fly out, fall under gravity, shrink, and cool down a warm palette ramp.

- **Effects use the game's own frames when they are there.** `gentex.drh`
  carries the retail explosion, flame and smoke animations as 64x64 tiles, and
  the engine already decompresses that bank and uploads it as an atlas. The mode
  checks the file exists, lets the existing loader do the work, and points its
  effect quads at the right frames. They are drawn masked, with palette index 0
  skipped rather than written -- every frame in that bank sits on index 0,
  between a third and nine tenths of each tile, so drawing them opaque would put
  a black square round every explosion. With no retail data it draws the
  flat-shaded particles instead and everything still runs -- the file check is
  what makes that true, because the stock loader exits the process on a missing
  bank rather than returning a failure.

- **Walls are built in panels, not slabs.** The legacy texture path works its
  coordinates out inside POLYTEX from the tile index and the projected polygon,
  and it fits exactly one tile to whatever polygon it is handed. A wall built as
  a single quad therefore wore one tile stretched two hundred metres wide and
  twenty high -- not a wall with a texture on it, a smear. Each side is now cut
  into panels the size of the floor's own tiles, so the wall and the ground
  agree about scale. It costs a few hundred quads an arena, which is why the
  tests now assert the budget rather than hoping: around 1750 quads for a full
  scene against a capacity of 4096, and nothing dropped.

- **Trees and rocks are cover with a different shape.** A tree is a trunk with
  two crowns stacked on it and a rock two boxes, all built out of the same
  panelled boxes the blocks are, so they collide, occlude and texture the way
  cover does. The grass, the canopy and the bark take their palette indices off
  the retail palette's own green and brown ramps rather than borrowing tracer
  colours: a field checkered green against grey read as a chess board.

- **Cover is panelled too.** Same reason as the walls, same tile size: a block
  twenty metres across wore one tile stretched over the whole face. Its sides
  are wound the other way round, because cover is seen from outside and a wall
  from inside, and it has no underside -- the only way to see one is to get
  beneath the world.

- **Surfaces use the game's own textures.** Ground, walls and cover are drawn
  from the retail texture banks when they are installed -- track1.drh for ground
  and walls, building.drh for the faces of cover -- through the loaders the
  engine already has. Each arena takes a different surface, and every surface
  keeps a palette index so a checkout with no retail data still comes up,
  flat-shaded, exactly as before.

- **The palette matters more than it looks.** With the retail data present the
  mode now loads palette.pal rather than its own fallback table. It has to:
  retail tiles are drawn in the retail palette's indices, so resolving them
  through a table that defines thirty colours turns tarmac into static.

- **Two retail fonts, not one.** minitext.bm is the restricted set the race HUD
  labels driver names and speed with, and it is what the small rows use. The
  title and the round banners use font6.bm, the larger sprite face the game
  announces things in. That one takes a colour, so a banner can still go green
  for a win and red for a loss.

- **Machines carry their weight.** Velocity is driven, not assigned. The race
  game's cars work the same way: a grip figure limits how fast sideways motion
  is corrected, whatever is left over bleeds off on its own, and a car cannot
  simply be told to be going somewhere else. Each machine has a grip, a drive
  acceleration and a brake, all in absolute metres per second squared. A siege
  platform sheds its old direction in about fourteen ticks and takes nineteen to
  reverse; an interceptor does both in four and seven.

- **Close quarters draws a sword.** The melee hitbox used to be an ordinary
  billboard: a bright square turned to face the camera, which reads as a shield
  held up rather than as anything being swung. It is a blade now -- pointed,
  level, running out along the line of the swing from the gun that threw it,
  with a crossguard at the hilt so it does not read as a spike. Built as two
  planes through the same axis, one flat and one upright, so it never turns
  edge-on and vanishes; there is no camera anywhere in the geometry, which is
  the point of it. Five quads, and the point lands just past the edge of the
  hitbox rather than well beyond it, because a blade drawn longer than its reach
  teaches a range the weapon does not have.

- **Hills stop bullets.** The floor used to be the plane `y = 0`, which was true
  of the first three arenas and of nothing since. A shot fired across Coldwater
  Meadow went through every hill it met, and one across Tower Seven went through
  the tabletop. `mecha_arena_trace_segment` now marches the segment against the
  same terrain query the ground mesh is built from, a metre at a stride, and
  bisects the step that straddles the surface, so the shape you can see is the
  shape that stops a shot. A platform arena has ground only where the platform
  is: a shot that leaves the roof keeps going down into the night rather than
  striking a floor that is not there.

- **The bodies lean, squat, ring and shake.** Four separate things, all drawn
  and none simulated, taken from the race game's own car code and kept in the
  pieces it keeps them in.

  Both games tilt a machine when you push the stick, and they do it in opposite
  directions: Whiplash winds a roll offset *against* the steering so a car leans
  out of the corner the way a body loads its outside springs, and Virtual-On's
  robots lean *into* it so the machine appears to answer faster than it does.
  The rates are the race game's own -- `iRollResponseRate` on,
  `iRollCenteringRate` off, clamped at `iMaxRollOffset` -- which works out at
  2.2 degrees for a car and a shade under 3 for a robot. A car below its
  steering floor does not lean at all, because below it the wheels do nothing
  either.

  The car also squats on the throttle and dives on the brakes, to
  `iMaxPitchOffset`: 1.76 degrees. In the air its nose follows the velocity
  vector rather than where it was pointed, from the arctangent of the climb
  against the run, clamped so a long drop off Tower Seven does not stand it on
  its nose.

  Landing rings. The attitude held at the moment of contact seeds a damped
  cosine about both axes, decaying at the rate the engine table blends by
  amplitude -- a bigger wobble dies faster, which is the one thing here that
  looks wrong written down and right on screen. A hard landing rings about seven
  and a half degrees and is visibly gone inside a second and a half. A bump is
  not a landing: without a fall-speed threshold a car crossing broken ground
  re-seeds the oscillator several times a second and shivers permanently.

  And they shake. Whiplash draws white noise on all three axes every frame,
  scaled by road speed multiplied by how wrecked the car is, so a healthy car at
  speed barely blurs (under half a degree), a wrecked one at speed shakes hard,
  and a wreck standing still sits perfectly quiet. A walking machine has no road
  speed to shake from, so what shakes it is being hit: the impulse is set where
  the damage lands, scaled against mass like everything else on that line, and
  bled off over about a second.

  Whiplash's control loop runs at 36 Hz and this one at 60, so every rate is
  scaled across and every decay factor raised to the ratio; the angles
  themselves carry over untouched, both games counting a full circle as 16384.

  One thing worth recording: **positive roll in the pose leans a machine left**,
  because it lifts the right side. That is not readable off the rotation matrix
  without also knowing the multiply order and whether the vectors are rows or
  columns. It was settled by building a mesh at a known roll and measuring which
  flank came out lower, and a test pins it that way. The same measurement showed
  the older travel lean had been leaning machines *away* from their direction of
  travel, against what its own comment said it was for; that is now negated too,
  so a machine boosting right leans right.

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

- **Leaving for the race is a tested path.** `--snapshot-scene arena-exit` boots
  into the arena, takes the exit, and draws the main menu, all headless -- which
  is how the crash behind "exit to Whiplash" was finally caught. It is not in
  the byte-exact baseline set, because what it is for is surviving the
  transition rather than any particular pixel; run it with
  `--frames 1 --out DIR` against a copy of the retail data.

## Known rough edges

- At point-blank range the player's own machine overlaps the target on screen.
  Inside knife range the camera centres the lock and drops your mech into the
  foreground, and two mechs in melee are simply in the same place. This wants
  tuning against real play rather than against a still frame.
- The clouds are the arena's own dome, not the retail one. `DrawHorizon` ends by
  drawing its dome, but that is forty quads ten million units out in a Z-up
  coordinate system, submitted through the renderer's cloud subdivision path;
  handing that an arena camera makes a frame take minutes instead of
  milliseconds, so the arena disables them at the `textures_off` bit while it
  calls in and hangs its own dome as ordinary arena geometry instead. Same five
  frames, same look, sorted and drawn like everything else in the scene.
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
