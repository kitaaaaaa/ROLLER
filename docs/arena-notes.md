# Arena mode: working notes

Long-form reasoning behind the arena mode's code. Source comments stay short
and point here by code, e.g. `[AI-02]`.

Each note records what was measured and what it ruled out, so a later change
does not re-litigate a question that already has an answer. Where a note says
"measured", the numbers came from running the simulation, not from reasoning
about it.

Codes are grouped by file: `AI-` mecha_ai.c, `MESH-` mecha_mesh.c,
`SIM-` mecha_sim.c, `ARENA-` mecha_arena.c, `REND-` mecha_render.c,
`DEF-` mecha_defs.c, `MODE-` mecha_mode.c, `TEST-` the test sources.

---

## AI-01 — the dodge margin does not scale with skill

`MECHA_AI_DODGE_MARGIN` is the same at every skill level, deliberately.

Scaling it with skill was tried and made the ladder run backwards. A pilot
that dodges more also dashes more; a dash changes its stance and swings it
off the firing cone, and the boost it burns eventually locks out, at which
point it cannot dodge at all. Measured over five duels, the wide-margin pilot
both dealt less damage and absorbed more than the middle rung.

Reaction time is the honest lever. How near a miss has to be before it is
worth answering is not.

## AI-02 — what actually separates the skill levels

The pilot reads the same world struct the simulation ticks, so it cannot be
made worse by hiding information from it — only by putting human limits back
in. The profile numbers were measured, and what they say is not what the
obvious design predicts.

**`iAimError` is the lever that works.** Every weapon aims itself at whatever
is locked, so with no error term the pilot fires a perfect solution every
time. Over twelve duels the damage it lands falls off cleanly once the error
clears the target's own width: about 23400 at zero, 19100 at nine degrees,
15500 at fourteen. Below roughly four degrees nothing happens at all — the
shot radius and the target radius swallow the error. Past about fourteen the
curve flattens again.

**`iReactionTicks` is not a strength lever**, however much it looks like one.
Sweeping it from zero to six tenths of a second moved the totals around
inside run-to-run variance and never in a consistent direction: a pilot that
answers every shot the instant it is fired also dashes constantly, and
dashing swings it off its own firing cone and drains the boost it needs to
dodge with. It is kept because it changes how the pilot *reads* — a rookie
visibly flinches late — not because it makes one harder to beat.

**`iTriggerOdds`** barely touches damage dealt, but hesitating measurably
raises damage absorbed, which is the half a losing player actually feels.

**`iTurnPercent`** arrived with the auto-turn being confined to knife range.
Before that a locked machine squared itself up for free at any distance, so
how well a pilot steered did not exist as a quality. Once pointing the
machine became the pilot's job, all three levels steered perfectly and the
ladder stopped meaning anything on the damage-taken half.

## AI-03 — look-ahead is braking distance, not a linear guess

`mecha_ai_stopping_look` originally used a stride plus a fixed fraction of
speed, and it undershot badly at the top end: a machine at seventy metres a
second checked forty-one metres ahead and needed eighty-six to stop, so by
the time an edge was inside its look-ahead it was already past saving.

Braking distance is `v² / 2a`, and the machine's own grip is that `a`, so the
number is available rather than guessable.

The larger of the two is taken. The linear guess is the better number for a
machine that stops hard — a walker's grip is three times a car's, so its
braking distance at walking pace is shorter than its own reaction time — and
the braking distance is better at the top end. Taking the larger means this
can only ever look further ahead than it used to.

The stride added on the front is reaction: a machine standing still still has
to not step off.

## AI-04 — spread weapons are scored by the share of cone that lands

Counting every pellet of a scattergun as a hit at any distance is what made
the gun car fire buckshot across the whole arena and never once reach for its
rifle: seven pellets of twenty-four scored as a hundred and sixty-eight
whether the target was fifteen metres away or a hundred and fifty.

A cone that wide only lands as a cone up close, so what is scored is the
share of it the target still covers.

## AI-05 — the footing rule runs last, and has three cases

An arena can have nothing underneath it. On a roof with a hole through the
middle, a pilot that only thought about the fight would walk into the pit.

There is no path-finding. There is only: do not step off, do not spend a
burst that ends off, and if you are already going off, cancel.

It runs after the dodging and after the lock has had its say about boosting
round to face someone, because any of those will happily spend a burst over
the edge.

A wall is not a hazard. Only an arena you can leave has an edge worth
avoiding; a pit is worth avoiding anywhere. Checking the boundary on a walled
arena would have the pilot backing away from walls it is entitled to fight
against.

Three states need separate handling:

- **Standing.** Letting go of the stick is not stopping — a machine just out
  of a burst is still travelling, so what it is carrying gets checked whether
  it asked for it or not.
- **Airborne.** There is no stepping back and nothing to brake against. All
  it can do with remaining thrust is lean towards the middle of the arena.
- **Mid-burst.** The burst is committed, so wanting to stop is not enough.
  The way out is the player's way: push back against it and boost again.

Roughly five per cent of rooftop fights still end in a walk-off, in the gaps
between those three states. Bounded by test rather than fixed.

## AI-06 — a boost cancel needs a fresh press

A burst is started by the press and a cancel needs another one, so a pilot
that simply leans on the boost button cancels nothing and rides the burst it
wanted to throw away straight off the edge. The input has to be released and
pressed again.

## AI-07 — a pilot saving its footing does not shoot

Firing locks a machine out of acting for the recovery, and a machine that
cannot act cannot steer. A shot taken while sliding towards an edge spends
the only ticks it had to stop itself. This was the last way computer pilots
were leaving the roof.

Held fire, by contrast, is applied at the trigger rather than earlier, so the
pilot goes on closing, circling and dodging exactly as it would. A machine
that stopped fighting would not show anything about how the fighting looks.

---

## MESH-01 — translucent quads carry a shade level, not a colour

`POLYFLAT` hands `SURFACE_FLAG_TRANSPARENT` polygons to `shadow_poly`, which
indexes `shade_palette[256 * level]` to darken what is already there.
`shade_palette` is 4096 bytes, so the level must stay under 16 or the read
runs off the end. The engine's own callers use 2 and 3 — `func2.c`'s
`blankwindow` and `replay.c`'s car shadows — so the mode's match.

## MESH-02 — walls are panelled because POLYTEX fits one tile per polygon

The legacy texture path works its coordinates out inside `POLYTEX` from the
tile index and the projected polygon, and fits exactly one tile to whatever
polygon it is given. A wall built as a single quad wears one tile stretched
two hundred metres wide and twenty high — a smear, not a texture. Cut into
panels the size of the floor's own tiles, each panel gets a tile at the scale
the ground is using and the two agree.

## MESH-03 — outer ground is rings, not a grid with a hole

Ground past the boundary cannot be walked on and is drawn coarsely, since it
is only ever seen at a distance. What it buys is that the arena stops being
an island.

It is built as rings of the boundary's own shape rather than a grid with the
middle knocked out, and that is not tidiness. A grid coarse enough to be
cheap has tiles far wider than the boundary is straight, so every tile it
drops for overlapping the arena takes a wedge of ground with it and the
horizon fills with holes, while every tile it keeps lies coplanar over the
arena's own floor. Rings share the edge exactly, so there is neither.

## MESH-04 — the walk cycle is paced by distance, not time

`fStepPhase` counts distance — one cycle every stride — so a machine that
stops mid-stride stops mid-stride, and a heavy one that covers ground slowly
takes slow steps without anything having to say so.

The thigh swings as a sine of the phase; the knee bends through the forward
half of that swing and straightens for the half the foot is on the ground
pushing back, which is the difference between walking and a pair of planks
pivoting at the hip.

Angles are positive forward and the caller negates them, because a positive
pitch in the pose matrix swings a limb backwards.

## MESH-05 — a glide runs on its own clock

Every other cycle is paced by distance. A boost breaks that: at seventy
metres a second, one stroke every five metres is fourteen cycles a second,
and legs moving that fast are a grey blur. The glide is timed instead — one
long push every two thirds of a second — which is what makes it read as
gliding rather than sprinting.

Boosting on the ground is not running. The thrusters do the work, so the legs
hold the machine up and steer it, which is a skater's problem: both knees
bent throughout, weight low, one leg reaching out and back in a long push
while the other glides underneath. The pushing leg straightens as it goes
out, which is what lets it stay on the floor at full stretch.

## MESH-06 — the ankle drop is what keeps feet on the floor

The body is lowered by whatever the straighter leg has lost, so bending the
knees sinks the machine instead of leaving it hanging in the air.

The thigh's pose pitch is `-A` and the knee's is `+K`, so the shin's frame
sits at `K - A` off the vertical and the ankle drops by the cosine of that.
Getting this sum wrong is the difference between a machine that walks and one
that skates with its feet through the floor.

## MESH-07 — attitude is summed in one place

Whiplash keeps the pieces apart all the way to the render pose and sums them
at the end (`car.c`: yaw takes the shake; pitch and roll take the landing
wobble, the shake and the control offset). The same three lines are all that
is needed here, and keeping them together makes it possible to read what a
body is doing without chasing the terms round the simulation.

## MESH-08 — a car's knockdown is a roll, not a pitch

Something tall enough to have a face pitches forward onto it. A machine nine
metres long and two high has nowhere to pitch to, and a Zizin standing on its
nose reads as a glitch rather than a wreck. The knockdown for a wheeled
machine is a half roll onto its roof.

## MESH-09 — the Zizin plan's axes are of opposite handedness

The body is the race game's own Zizin, polygon for polygon: `xzizin_coords`
and `xzizin_pols` out of `carplans.c`, the same fifty quads the car is drawn
with on the track.

The plan is in the race game's axes — x along the car, y across it, z up —
where the arena's are x across, y up, z forward. The three swap and the
lateral one is negated. That negation is what keeps the car the right way
round: the two frames are of opposite handedness, so swapping axes alone
builds the car's reflection, with wheel arches, exhausts and both flanks of
its livery on the wrong sides.

Reflecting it back reverses every winding the plan had, which is why its
panels face inwards here and why artwork on them needs different treatment
from the rest of the mode.

## MESH-10 — three kinds of texture word in the plan

The race game's own draw path walks all three:

- Most panels carry a texture word: `APPLY_TEXTURE` set, tile in the low byte.
- Eight — the wheels and the livery — carry `ANMS_LOOKUP`, where the low byte
  indexes the car's animation table and the real word is a frame out of it.
  Frame zero is the one at rest.
- The rest carry no texture flag and the low byte is a plain palette index,
  which is how the tyres come out black.

## MESH-11 — the fifty panels do not need a sorted list

The race game draws this body through a sorted polygon list of its own — the
`nNextPolIdx` links in the plan — and a painter's algorithm has no such list,
so two panels sharing a plane would flicker. They do not: the fifty are
tested against each other by the coplanar check, and the body is rigid, so
passing at one pose is passing at all of them.

## MESH-12 — panel orientation comes from the plan, not from us

Each polygon carries `SURFACE_FLAG_FLIP_HORIZ` and `SURFACE_FLAG_FLIP_VERT`
beside its tile index. That is how the body wears one tile across a pair of
mirrored panels — roof rails, rear roof edge, lower tail corners — and it is
why painting it panel for panel out of the same file still came out back to
front: those bits were being dropped and the orientation guessed at
afterwards, one panel at a time, from renders.

Read the flags off the surface the lookup settled on, not off the polygon.
This matters for the wheels: those four name an animation slot and carry no
orientation of their own, while the frames behind them do — the near-side
pair flipped, the off-side pair not. Taking the polygon's word for it left
all four wheels wearing the same face.

Dropping the corner reversal is the vertical mirror, so horizontal is that
reversal plus two quarter turns, and the two together are a half turn with no
reflection at all. `MECHA_QUAD_TEX_ROT90` and `MECHA_QUAD_TEX_ROT180` form a
two-bit turn count, so all eight arrangements are reachable.

## MESH-13 — the gun car's gun

A handgun about as long as the car, attached to nothing: it floats off the
front right wheel, held over on its side so the slide is horizontal and the
shot goes out across the bonnet rather than over the roof. That roll is what
puts the grip out to the left instead of underneath. No arms, no turret, just
an absurd pistol keeping station beside a race car.

Firing throws it up and back. The recovery counts down from the shot, so the
kick is hardest on the tick it goes off and has run out by the time the next
round is chambered. The same kick shoves the car in the simulation, so what
is drawn is what happened.

## MESH-14 — rolling over lifts the body back onto the floor

The pose turns about the car's own floor, so half a roll puts the whole body
below it: a point at height `h` lands at `h cos t`, and at 180 degrees the
roof is a full height underground. Raising the origin by however far the
lowest corner has gone under keeps the car resting on the floor the whole way
over — a car rolling, rather than a car sinking into the tarmac.

## MESH-15 — the lean is negated

Positive roll lifts the machine's right side and so leans the machine left.
Established by building a mesh at a known roll and measuring which flank came
out lower, after reasoning about it got the sign wrong twice.

Without the negation the machine leant away from its direction of travel. A
machine boosting to its right leans right, as anything on wheels or blades
does.

## MESH-16 — the planting solve opens the hips, not the knees

Both feet down: the floor is as far as the shorter leg can reach once its own
hip roll is counted, and the other leg makes up the difference by rolling
further out.

That is how the pose works rather than a fudge. A skater at full stretch has
its pushing leg out to the side precisely because it is straight, and a
machine standing with its feet apart has its hips open for the same reason.
Take the difference out of the knees instead and the stance has no width.

## MESH-17 — hip roll is its own frame, above the swing

Rolled first and swung afterwards, the whole leg tips outwards as one and its
foot lands exactly `cos(roll)` of the way down, which is what lets the
planting solve pick a roll and be right.

Roll the thigh itself instead and the swing happens in the unrolled plane,
the two rotations no longer commute, and the feet miss the floor.

## MESH-18 — the knee stands proud of the limb

It is how the reference art draws a knee, and it is the only thing keeping
its faces out of their planes: a joint the same width as the limb it sits on
has coplanar sides with it the moment the joint angle passes through
straight, and there is no depth buffer to sort that out.

## MESH-19 — the ankle cancels the hip roll

The foot stays flat to the floor whatever the leg above it is doing, both
ways. The three pitches up the chain cancel to nothing by construction, so
what is left of the hip above the ankle is the roll alone, and giving the
ankle the same roll back undoes it exactly. Without it a splayed leg lands on
the outer edge of its foot and drives the inner corner through the floor.

## MESH-20 — an idle machine lets its arms down

A machine with nothing locked and nothing in flight lets the whole chain
unfold: the shoulder stops tracking, the elbow gives up its right angle, and
the guns end up pointed at the floor. It is the only way to tell at a glance
which of two machines across the arena is about to shoot, and it costs
nothing to read.

## MESH-21 — mirroring a sprite means reversing its corners

`POLYTEX` takes its texture coordinates from the projected corners, so a quad
always carries the whole tile however wide it is drawn. The way to mirror a
sprite is therefore to reverse the order its corners arrive in, which is what
`MECHA_QUAD_TEX_FLIP` switches. Two half-billboards side by side, one
flipped, are one sprite and its own reflection meeting down the middle.

## MESH-22 — the blade is geometry, not a billboard

It used to be an ordinary billboard: a bright square facing the camera, which
read as a shield held up rather than anything being swung. A close-quarters
weapon wants a shape with direction in it.

Built as two planes through the same axis, one flat and one upright, so it
never turns edge-on and vanishes — there is no camera in the geometry at all,
which is the point. Each plane is a tapering body and a point, and a short
crossguard at the hilt stops the whole thing reading as a spike.

## MESH-23 — the depth key, and the two cases the middle gets wrong

There is no depth buffer, so a quad is drawn either before another or after
it, whole. For most geometry the middle of the quad is the honest answer to
which. Two cases it is not:

- **A shadow lying on the floor.** Its middle can easily be further off than
  the middle of a floor tile it covers, and the tile is then painted over it,
  cutting the shadow along a tile edge that moves with the camera. Sorting
  the decal by its nearest corner and the ground by its farthest fixes it
  both ways round.
- **Broad horizontal surfaces**, the same argument from the other side: a
  floor tile stretching away under a machine standing on it has to be drawn
  first, and its far corner is what says so.

## MESH-24 — self-lit geometry is pulled forward in the sort

A blast centred on a machine intersects it, and per-quad sorting then lets
some panels paint over the fireball and not others — a fireball with a hole
in it that swims about as the camera moves.

Pulling the key forward by the sprite's own half-width, which is the radius
of the volume it stands for, sorts it as though it stood clear in front:
everything inside that volume is outranked, everything outside is not, so a
shoulder well clear of the fireball still occludes it. The narrower of the
two edges is measured, because a long thin tracer has no business claiming to
be half its length nearer than it is.

## MESH-25 — the sky dome radius is chosen against the arena

The floor is a couple of hundred metres across, so a dome at six hundred
swung by nearly twenty degrees as a player crossed it — the sky sliding
rather than the machine walking. At fourteen hundred it is a few degrees.

Puff size scales with radius, so pushing it out costs nothing but parallax,
and it is still four orders of magnitude short of straining a float. The
retail dome sits ten million units out, which suits a track renderer written
around it and does not suit this one.

## MESH-26 — the forest grows outwards off the boundary

The trees a machine can hide behind are boxes built from the same panels as
the cover. These are the other hundred and fifty: the game's own tree
sprites, upright and camera-facing, with nothing behind them. They do not
collide, do not block a shot and are not on the ground mesh. They are there
so an arena with an invisible boundary reads as a clearing in a wood rather
than a field that stops.

Placement is a hash of the tree's index and the match seed, so nothing is
stored between frames and the same match always grows the same forest.

The draw is squared rather than uniform. A square scatter puts as many trees
five hundred metres away as fifty, which is a thin haze on the horizon and
nothing at the edge — and the edge is the point, since that is where the
invisible wall is. Squaring crowds them against the boundary and thins them
behind.

## MESH-27 — a tracer's streak keeps the weapon's colour

That colour is how a player tells whose fire is crossing the arena. A
textured quad draws the frame's colours and nothing else, so a plasma-skinned
streak would make every machine's beams the same blue. The head is small
enough to read as the glow at the front of the bolt rather than the bolt.

## MESH-28 — a blast opens fast and collapses

There is no alpha in an indexed frame buffer, so size is the only thing
carrying the shape of the blast. The earlier curve grew all the way to full
scale at the end of its life, so a blast covered the most screen on the last
frame before vanishing — which reads as the arena being blanked and restored
rather than something exploding. Peaking a third of the way in and shrinking
from there reads as a burst.

## MESH-29 — debris cools as it falls

The ramp runs from the pale gold at the top of the sky gradient back down
through orange into the deep reds at its zenith. The shared indices are not a
coincidence worth fighting: they are the one contiguous warm ramp the palette
has, they read as heat in either palette, and a particle walking them
downwards is a particle going out.

## MESH-30 — the jetpack flame is two mirrored halves

The fire tiles are drawn leaning one way, so a single one reads as a flame
blown sideways — wrong for something pointing straight down out of a
jetpack. Two halves meeting down the middle, the right-hand copy mirrored,
cost one extra quad and no overlap, so nothing is drawn twice into the same
pixels and the painter's order has nothing to decide.

---

## REND-01 — the mode carries its own font

ROLLER's HUD font lives in the retail sprite blocks, which the rest of this
mode deliberately does without: mechs, arena and effects are all generated
rather than loaded. A HUD that needed game data would be the one asset
dependency in an otherwise self-contained mode, so the mode carries a
five-by-seven face as a fallback. Each glyph is seven rows of five bits, most
significant bit leftmost.

`minitext.bm` is the small face the race HUD prints speed and gear with and
is what the mode uses when it is present; `font6.bm` is the larger sprite
face the game announces things in, and is what the title and round banners
want. The two are not interchangeable.

Two things about the retail path matter. Glyphs are indexed through
`ascii_conv3` (or `font6_ascii` for the large face), where 255 means "no
glyph" and costs a flat four pixels of advance. And `prt_letter` scales
through the `scr_size` global rather than an argument, pre-multiplying the
coordinates it is handed — so drawing at `iScale` means setting the global
and passing coordinates that have *not* been scaled.

## REND-02 — the camera does not dodge occlusion

It used to: a segment trace to whatever it was looking at, and up to six
three-metre steps upward until the line came clear. That was always eager —
it swung the whole arena for one pillar — and it got much worse once the
ground itself began blocking that trace, because then every hill the player
drove behind heaved the camera into the air.

Virtual-On does not move the camera for this at all. It leaves the camera
where it belongs and turns whatever is in the way transparent, which keeps
the frame still and tells the player exactly what is happening. That wants a
renderer that can blend, so it is not written yet. Until it is, nothing
happens, which is better than the wrong thing happening quickly.

The floor clamp is not this and stays: keeping the camera out of the ground
is not occlusion avoidance.

## REND-03 — the chase rig scales with the machine

The chase is written around a machine fourteen metres tall, which is most of
the roster. The car is a sixth of that and would be a speck under a camera
hung fourteen metres up, so the rig scales.

Not all the way down: a car doing seventy metres a second needs to see
further ahead than two metres of camera height gives it, and the floor clamp
is what stops the view ending up in the bodywork.

## REND-04 — the camera follows the player's heading, not the enemy's bearing

It used to swing onto the bearing to the enemy at every range, so the view
turned when the enemy moved rather than when the player did — and with the
machine no longer squaring itself up outside knife range, the camera pointed
somewhere the machine was not.

## REND-05 — near-plane clipping keeps the polygon a quad

`game_render_quad_world` accepts quads only, so a vertex behind the near
plane is pulled forward along an edge that crosses it rather than the polygon
being split. This is what stops a floor tile the camera is standing on from
smearing across the screen when the rasteriser clamps its z.

## REND-06 — banks are resolved before anything is built

The mech mesh is the first thing to ask which banks are loaded, so answering
with last frame's result left the car in flat paint for its first frame.

The effect bank loads itself, because the first shot fired names it and the
draw path loads whatever a quad names. The car's skin has no such trigger —
the mesh will not name a bank it has been told is missing, and the bank stays
missing because nothing named it — so it is asked for explicitly, and only
when there is something in the fight to wear it.

## REND-07 — how the banks load, and what the loaders get wrong

Every bank is loaded by a routine the game already has; nothing here parses a
`.DRH`. What this owns is the part those routines are careless about.

Each calls `ErrorBoxExit` when its file is missing — taking the process down
rather than returning a failure — so each is probed first, and a bank that is
not there simply never becomes available. This matters most for
`LoadGenericCarTextures`: on a checkout with no retail data it would kill the
process instead of falling back, and falling back is the whole point. Every
effect still carries a palette index, so a mode with no bank draws what it
drew before.

Each uploads through `g_pGameRenderer`, the global the race sets up, so a
mode drawing on its own renderer gets the decompress and the sort but no
upload. The pixels are left in a global either way, so they are handed to the
renderer that is actually drawing.

The engine's own numbering is not exposed past that table: the track bank is
bank 0 while its tile count lives at `num_textures[19]`, and that is not a
quirk worth spreading through the mesh.

## REND-08 — the low byte means different things on different paths

`POLYFLAT` takes its colour from the low byte of the surface flags and routes
anything marked transparent through `shadow_poly`. On that path the low byte
is a shade *level*, not a colour, and `shade_palette` holds only 16 blocks —
so the value is masked. A bad colour is a visible bug; a bad read is not.

On the textured path the low byte is a *tile index*, which is the easiest
thing on it to get wrong: a colour left in those bits names a tile the bank
does not have, the renderer rejects it, and the quad quietly comes out flat.

`PARTIAL_TRANS` is what makes a frame a sprite rather than a black square: on
that path index 0 is skipped instead of written, and every effect frame is
drawn on index 0, with between a third and nine tenths of each tile
background.

## REND-09 — POLYTEX derives its own coordinates, so corner order is the API

The legacy path works its texture coordinates out inside `POLYTEX` from the
tile index and the projected polygon; the track renderer passes zeroes on
every vertex and always has.

So the order the four corners arrive in decides how the tile lies on them,
and the arena winds its quads the other way round the face from the track.
Nothing else in the mode noticed: this renderer rejects back faces off the
stored normal rather than the projected winding, so a quad wound backwards
still culls, sorts and fills correctly, and every texture it had worn —
grass, tarmac, concrete, a plasma bolt — was near enough symmetrical to look
right mirrored. Put lettering on one and it reads backwards.

Geometry the mode builds itself is therefore handed over reversed. Geometry
out of the game's own files is not: it arrived already reflected by the frame
change that got it here. See [MESH-12] for the flags that decide the rest.

## REND-10 — the briefing's line budget

The game's smaller video mode gives this a 320x200 buffer, and at 200 pixels
there is room for exactly twenty-five lines. Anything that does not fit is
lost off the bottom, and the bottom is where the exit row lives.

Besides the rows themselves: two lines for the double-height title, one for
the result and a blank after it, a blank either side of the rows, the footer,
and one more as the margin the footer's glyphs need.

The controls used to be printed here, ten lines of them, which is most of why
the rows had nowhere to grow. They are on a page of their own now — still one
keypress away, no longer in the way.

## REND-11 — the sky is DrawHorizon, and the clouds are off

`DrawHorizon` paints the sky, the same routine the race uses: two flat fills
split by a line through the projection, blue above and a haze colour below.
The arena had a nine-band sunset gradient before this, which looked well
enough on a still frame but was the mode inventing a sky the engine already
had, and it could never carry clouds.

What `DrawHorizon` reads, it reads from globals. Most are already written by
the time this runs — `game_render_set_camera` and `set_projection` push
`viewx`, the vk basis, `xbase`, `ybase`, `scr_size` and `VIEWDIST` through
for exactly this kind of legacy path — so what is left is elevation, tilt and
colour.

The clouds are disabled. That dome is real geometry: forty quads placed ten
million units out, in the track code's coordinate system where the up axis is
Z rather than Y, submitted through the renderer's cloud subdivision path.
Handing that path an arena camera makes it subdivide quads that size until
the frame stops arriving — a run that takes a fifth of a second takes
minutes. Getting them in wants the dome rebuilt against the arena's own scale
and axes, rather than the basis swapped underneath it.

## REND-12 — why these palette indices

The names live next to the code that uses them; the palette table is where
those indices get colours for the case where no palette has been loaded.

The indices themselves were chosen by matching the intended colours against
the retail palette, so a player with the game data sees roughly the same
picture from their own `PALETTE.PAL` rather than whatever sits at an
arbitrary index. That palette is mostly a grey ramp between 115 and 143 with
saturated primaries higher up, which is why the arena reads as grey structure
with coloured tracers. Some indices are deliberately shared — a tracer and a
HUD accent — because the mode paints into a palette it does not own all of.

The sky bands climb steadily in brightness whether resolved through the
retail palette or the fallback: 221-230 is a dark-to-bright red ramp, 167-171
orange, 204-207 the top of a yellow. **231 is deliberately unused.** It is
the obvious brightest red to finish on, and it is also the low-armour
warning: a sky matching the colour of "you are about to die" hides it.

## REND-13 — the recoloured effect banks

The game's plasma frames are blue and there is only one set, so every
machine's fire came out the same colour. In a crossfire you could not tell
whose shot was whose, which is the one thing a shot has to say.

A tint is built by walking each frame's palette indices onto the nearest
colour the palette has in the wanted hue at the same brightness, and
uploading the result as a bank of its own — so the recolour is real pixels
rather than a shading trick the rasteriser does not have. Index 0 stays index
0: that is the transparent key and everything about these frames depends on
it.

Slots 20 and up are used. The engine's texture-count table only ever speaks
for 0, 17, 18 and 19, so the rest are free for the arena to take, and the
count is set alongside the upload.

---

## SIM-01 — an empty gauge takes the thrust, not the legs

The gauge gates four things. Only three of them are thrust:

| action | needs gauge |
|---|---|
| jump takeoff | no |
| hover (holding jump) | yes |
| dash, air dash | yes |
| jump cancel | no |

Leaving the ground is the legs' work, so an empty machine still jumps and
still cancels out of the jump. What it loses is the thrust to hang in the
air with and to dash with.

The takeoff used to be gated on the gauge, which meant an empty machine
could not get airborne at all — and so could not jump-cancel either, since
the cancel needs a jump to cancel. The lockout cooldown on spending the last
of the gauge is deliberate and stays; what it should cost is hover and dash,
not the ability to leave the ground.

The cost is charged only when there is gauge to charge. Otherwise a locked
machine mashing jump would spend the recovery it needs in order to unlock,
and never climb back out.
