# Arena mode: working notes

Long-form reasoning behind the arena mode's code. Source comments stay short and
point here by code, e.g. `[AI-02]`.

Each note records what was measured and what it ruled out, so a later change
does not re-litigate a question that already has an answer. Where a note says
"measured", the numbers came from running the simulation, not from reasoning
about it.

Codes are grouped by file: `AI-` mecha_ai.c, `MESH-` mecha_mesh.c, `SIM-`
mecha_sim.c, `ARENA-` mecha_arena.c, `REND-` mecha_render.c, `DEF-`
mecha_defs.c, `MODE-` mecha_mode.c, `TEST-` the test sources.

______________________________________________________________________

## AI-01 — the dodge margin does not scale with skill

`MECHA_AI_DODGE_MARGIN` is the same at every skill level, deliberately.

Scaling it with skill was tried and made the ladder run backwards. A pilot that
dodges more also dashes more; a dash changes its stance and swings it off the
firing cone, and the boost it burns eventually locks out, at which point it
cannot dodge at all. Measured over five duels, the wide-margin pilot both dealt
less damage and absorbed more than the middle rung.

Reaction time is the honest lever. How near a miss has to be before it is worth
answering is not.

## AI-02 — what actually separates the skill levels

The pilot reads the same world struct the simulation ticks, so it cannot be made
worse by hiding information from it — only by putting human limits back in. The
profile numbers were measured, and what they say is not what the obvious design
predicts.

**`iAimError` is the lever that works.** Every weapon aims itself at whatever is
locked, so with no error term the pilot fires a perfect solution every time.
Over twelve duels the damage it lands falls off cleanly once the error clears
the target's own width: about 23400 at zero, 19100 at nine degrees, 15500 at
fourteen. Below roughly four degrees nothing happens at all — the shot radius
and the target radius swallow the error. Past about fourteen the curve flattens
again.

**`iReactionTicks` is not a strength lever**, however much it looks like one.
Sweeping it from zero to six tenths of a second moved the totals around inside
run-to-run variance and never in a consistent direction: a pilot that answers
every shot the instant it is fired also dashes constantly, and dashing swings it
off its own firing cone and drains the boost it needs to dodge with. It is kept
because it changes how the pilot *reads* — a rookie visibly flinches late — not
because it makes one harder to beat.

**`iTriggerOdds`** barely touches damage dealt, but hesitating measurably raises
damage absorbed, which is the half a losing player actually feels.

**`iTurnPercent`** arrived with the auto-turn being confined to knife range.
Before that a locked machine squared itself up for free at any distance, so how
well a pilot steered did not exist as a quality. Once pointing the machine
became the pilot's job, all three levels steered perfectly and the ladder
stopped meaning anything on the damage-taken half.

## AI-03 — look-ahead is braking distance, not a linear guess

`mecha_ai_stopping_look` originally used a stride plus a fixed fraction of
speed, and it undershot badly at the top end: a machine at seventy metres a
second checked forty-one metres ahead and needed eighty-six to stop, so by the
time an edge was inside its look-ahead it was already past saving.

Braking distance is `v² / 2a`, and the machine's own grip is that `a`, so the
number is available rather than guessable.

The larger of the two is taken. The linear guess is the better number for a
machine that stops hard — a walker's grip is three times a car's, so its braking
distance at walking pace is shorter than its own reaction time — and the braking
distance is better at the top end. Taking the larger means this can only ever
look further ahead than it used to.

The stride added on the front is reaction: a machine standing still still has to
not step off.

## AI-04 — spread weapons are scored by the share of cone that lands

Counting every pellet of a scattergun as a hit at any distance is what made the
gun car fire buckshot across the whole arena and never once reach for its rifle:
seven pellets of twenty-four scored as a hundred and sixty-eight whether the
target was fifteen metres away or a hundred and fifty.

A cone that wide only lands as a cone up close, so what is scored is the share
of it the target still covers.

## AI-05 — the footing rule runs last, and has three cases

An arena can have nothing underneath it. On a roof with a hole through the
middle, a pilot that only thought about the fight would walk into the pit.

There is no path-finding. There is only: do not step off, do not spend a burst
that ends off, and if you are already going off, cancel.

It runs after the dodging and after the lock has had its say about boosting
round to face someone, because any of those will happily spend a burst over the
edge.

A wall is not a hazard. Only an arena you can leave has an edge worth avoiding;
a pit is worth avoiding anywhere. Checking the boundary on a walled arena would
have the pilot backing away from walls it is entitled to fight against.

Three states need separate handling:

- **Standing.** Letting go of the stick is not stopping — a machine just out of
  a burst is still travelling, so what it is carrying gets checked whether it
  asked for it or not.
- **Airborne.** There is no stepping back and nothing to brake against. All it
  can do with remaining thrust is lean towards the middle of the arena.
- **Mid-burst.** The burst is committed, so wanting to stop is not enough. The
  way out is the player's way: push back against it and boost again.

Roughly five per cent of rooftop fights still end in a walk-off, in the gaps
between those three states. Bounded by test rather than fixed.

## AI-06 — a boost cancel needs a fresh press

A burst is started by the press and a cancel needs another one, so a pilot that
simply leans on the boost button cancels nothing and rides the burst it wanted
to throw away straight off the edge. The input has to be released and pressed
again.

## AI-07 — a pilot saving its footing does not shoot

Firing locks a machine out of acting for the recovery, and a machine that cannot
act cannot steer. A shot taken while sliding towards an edge spends the only
ticks it had to stop itself. This was the last way computer pilots were leaving
the roof.

Held fire, by contrast, is applied at the trigger rather than earlier, so the
pilot goes on closing, circling and dodging exactly as it would. A machine that
stopped fighting would not show anything about how the fighting looks.

______________________________________________________________________

## MESH-01 — translucent quads carry a shade level, not a colour

`POLYFLAT` hands `SURFACE_FLAG_TRANSPARENT` polygons to `shadow_poly`, which
indexes `shade_palette[256 * level]` to darken what is already there.
`shade_palette` is 4096 bytes, so the level must stay under 16 or the read runs
off the end. The engine's own callers use 2 and 3 — `func2.c`'s `blankwindow`
and `replay.c`'s car shadows — so the mode's match.

## MESH-02 — walls are panelled because POLYTEX fits one tile per polygon

The legacy texture path works its coordinates out inside `POLYTEX` from the tile
index and the projected polygon, and fits exactly one tile to whatever polygon
it is given. A wall built as a single quad wears one tile stretched two hundred
metres wide and twenty high — a smear, not a texture. Cut into panels the size
of the floor's own tiles, each panel gets a tile at the scale the ground is
using and the two agree.

## MESH-03 — outer ground is rings, not a grid with a hole

Ground past the boundary cannot be walked on and is drawn coarsely, since it is
only ever seen at a distance. What it buys is that the arena stops being an
island.

It is built as rings of the boundary's own shape rather than a grid with the
middle knocked out, and that is not tidiness. A grid coarse enough to be cheap
has tiles far wider than the boundary is straight, so every tile it drops for
overlapping the arena takes a wedge of ground with it and the horizon fills with
holes, while every tile it keeps lies coplanar over the arena's own floor. Rings
share the edge exactly, so there is neither.

## MESH-04 — the walk cycle is paced by distance, not time

`fStepPhase` counts distance — one cycle every stride — so a machine that stops
mid-stride stops mid-stride, and a heavy one that covers ground slowly takes
slow steps without anything having to say so.

The thigh swings as a sine of the phase; the knee bends through the forward half
of that swing and straightens for the half the foot is on the ground pushing
back, which is the difference between walking and a pair of planks pivoting at
the hip.

Angles are positive forward and the caller negates them, because a positive
pitch in the pose matrix swings a limb backwards.

## MESH-05 — a glide runs on its own clock

Every other cycle is paced by distance. A boost breaks that: at seventy metres a
second, one stroke every five metres is fourteen cycles a second, and legs
moving that fast are a grey blur. The glide is timed instead — one long push
every two thirds of a second — which is what makes it read as gliding rather
than sprinting.

Boosting on the ground is not running. The thrusters do the work, so the legs
hold the machine up and steer it, which is a skater's problem: both knees bent
throughout, weight low, one leg reaching out and back in a long push while the
other glides underneath. The pushing leg straightens as it goes out, which is
what lets it stay on the floor at full stretch.

## MESH-06 — the ankle drop is what keeps feet on the floor

The body is lowered by whatever the straighter leg has lost, so bending the
knees sinks the machine instead of leaving it hanging in the air.

The thigh's pose pitch is `-A` and the knee's is `+K`, so the shin's frame sits
at `K - A` off the vertical and the ankle drops by the cosine of that. Getting
this sum wrong is the difference between a machine that walks and one that
skates with its feet through the floor.

## MESH-07 — attitude is summed in one place

Whiplash keeps the pieces apart all the way to the render pose and sums them at
the end (`car.c`: yaw takes the shake; pitch and roll take the landing wobble,
the shake and the control offset). The same three lines are all that is needed
here, and keeping them together makes it possible to read what a body is doing
without chasing the terms round the simulation.

## MESH-08 — a car's knockdown is a roll, not a pitch

Something tall enough to have a face pitches forward onto it. A machine nine
metres long and two high has nowhere to pitch to, and a Zizin standing on its
nose reads as a glitch rather than a wreck. The knockdown for a wheeled machine
is a half roll onto its roof.

## MESH-09 — the Zizin plan's axes are of opposite handedness

The body is the race game's own Zizin, polygon for polygon: `xzizin_coords` and
`xzizin_pols` out of `carplans.c`, the same fifty quads the car is drawn with on
the track.

The plan is in the race game's axes — x along the car, y across it, z up — where
the arena's are x across, y up, z forward. The three swap and the lateral one is
negated. That negation is what keeps the car the right way round: the two frames
are of opposite handedness, so swapping axes alone builds the car's reflection,
with wheel arches, exhausts and both flanks of its livery on the wrong sides.

Reflecting it back reverses every winding the plan had, which is why its panels
face inwards here and why artwork on them needs different treatment from the
rest of the mode.

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
`nNextPolIdx` links in the plan — and a painter's algorithm has no such list, so
two panels sharing a plane would flicker. They do not: the fifty are tested
against each other by the coplanar check, and the body is rigid, so passing at
one pose is passing at all of them.

## MESH-12 — panel orientation comes from the plan, not from us

Each polygon carries `SURFACE_FLAG_FLIP_HORIZ` and `SURFACE_FLAG_FLIP_VERT`
beside its tile index. That is how the body wears one tile across a pair of
mirrored panels — roof rails, rear roof edge, lower tail corners — and it is why
painting it panel for panel out of the same file still came out back to front:
those bits were being dropped and the orientation guessed at afterwards, one
panel at a time, from renders.

Read the flags off the surface the lookup settled on, not off the polygon. This
matters for the wheels: those four name an animation slot and carry no
orientation of their own, while the frames behind them do — the near-side pair
flipped, the off-side pair not. Taking the polygon's word for it left all four
wheels wearing the same face.

Dropping the corner reversal is the vertical mirror, so horizontal is that
reversal plus two quarter turns, and the two together are a half turn with no
reflection at all. `MECHA_QUAD_TEX_ROT90` and `MECHA_QUAD_TEX_ROT180` form a
two-bit turn count, so all eight arrangements are reachable.

## MESH-13 — the gun car's gun

A handgun about as long as the car, attached to nothing: it floats off the front
right wheel, held over on its side so the slide is horizontal and the shot goes
out across the bonnet rather than over the roof. That roll is what puts the grip
out to the left instead of underneath. No arms, no turret, just an absurd pistol
keeping station beside a race car.

Firing throws it up and back. The recovery counts down from the shot, so the
kick is hardest on the tick it goes off and has run out by the time the next
round is chambered. The same kick shoves the car in the simulation, so what is
drawn is what happened.

## MESH-14 — rolling over lifts the body back onto the floor

The pose turns about the car's own floor, so half a roll puts the whole body
below it: a point at height `h` lands at `h cos t`, and at 180 degrees the roof
is a full height underground. Raising the origin by however far the lowest
corner has gone under keeps the car resting on the floor the whole way over — a
car rolling, rather than a car sinking into the tarmac.

## MESH-15 — the lean is negated

Positive roll lifts the machine's right side and so leans the machine left.
Established by building a mesh at a known roll and measuring which flank came
out lower, after reasoning about it got the sign wrong twice.

Without the negation the machine leant away from its direction of travel. A
machine boosting to its right leans right, as anything on wheels or blades does.

## MESH-16 — the planting solve opens the hips, not the knees

Both feet down: the floor is as far as the shorter leg can reach once its own
hip roll is counted, and the other leg makes up the difference by rolling
further out.

That is how the pose works rather than a fudge. A skater at full stretch has its
pushing leg out to the side precisely because it is straight, and a machine
standing with its feet apart has its hips open for the same reason. Take the
difference out of the knees instead and the stance has no width.

## MESH-17 — hip roll is its own frame, above the swing

Rolled first and swung afterwards, the whole leg tips outwards as one and its
foot lands exactly `cos(roll)` of the way down, which is what lets the planting
solve pick a roll and be right.

Roll the thigh itself instead and the swing happens in the unrolled plane, the
two rotations no longer commute, and the feet miss the floor.

## MESH-18 — the knee stands proud of the limb

It is how the reference art draws a knee, and it is the only thing keeping its
faces out of their planes: a joint the same width as the limb it sits on has
coplanar sides with it the moment the joint angle passes through straight, and
there is no depth buffer to sort that out.

## MESH-19 — the ankle cancels the hip roll

The foot stays flat to the floor whatever the leg above it is doing, both ways.
The three pitches up the chain cancel to nothing by construction, so what is
left of the hip above the ankle is the roll alone, and giving the ankle the same
roll back undoes it exactly. Without it a splayed leg lands on the outer edge of
its foot and drives the inner corner through the floor.

## MESH-20 — an idle machine lets its arms down

A machine with nothing locked and nothing in flight lets the whole chain unfold:
the shoulder stops tracking, the elbow gives up its right angle, and the guns
end up pointed at the floor. It is the only way to tell at a glance which of two
machines across the arena is about to shoot, and it costs nothing to read.

## MESH-21 — mirroring a sprite means reversing its corners

`POLYTEX` takes its texture coordinates from the projected corners, so a quad
always carries the whole tile however wide it is drawn. The way to mirror a
sprite is therefore to reverse the order its corners arrive in, which is what
`MECHA_QUAD_TEX_FLIP` switches. Two half-billboards side by side, one flipped,
are one sprite and its own reflection meeting down the middle.

## MESH-22 — the blade is geometry, not a billboard

It used to be an ordinary billboard: a bright square facing the camera, which
read as a shield held up rather than anything being swung. A close-quarters
weapon wants a shape with direction in it.

Built as two planes through the same axis, one flat and one upright, so it never
turns edge-on and vanishes — there is no camera in the geometry at all, which is
the point. Each plane is a tapering body and a point, and a short crossguard at
the hilt stops the whole thing reading as a spike.

## MESH-23 — the depth key, and the two cases the middle gets wrong

There is no depth buffer, so a quad is drawn either before another or after it,
whole. For most geometry the middle of the quad is the honest answer to which.
Two cases it is not:

- **A shadow lying on the floor.** Its middle can easily be further off than the
  middle of a floor tile it covers, and the tile is then painted over it,
  cutting the shadow along a tile edge that moves with the camera. Sorting the
  decal by its nearest corner and the ground by its farthest fixes it both ways
  round.
- **Broad horizontal surfaces**, the same argument from the other side: a floor
  tile stretching away under a machine standing on it has to be drawn first, and
  its far corner is what says so.

That second rule applies only to quads carrying `MECHA_QUAD_GROUND`, which the
arena sets and machines never do. Keyed on the normal alone it also caught a
car's own floor and roof — both horizontal, both wider than the threshold — so
the two halves of the body sorted against each other by whichever was broader.
Upright that happens to look right; upside down the two swap and the error
shows.

## MESH-24 — self-lit geometry is pulled forward in the sort

A blast centred on a machine intersects it, and per-quad sorting then lets some
panels paint over the fireball and not others — a fireball with a hole in it
that swims about as the camera moves.

Pulling the key forward by the sprite's own half-width, which is the radius of
the volume it stands for, sorts it as though it stood clear in front: everything
inside that volume is outranked, everything outside is not, so a shoulder well
clear of the fireball still occludes it. The narrower of the two edges is
measured, because a long thin tracer has no business claiming to be half its
length nearer than it is.

## MESH-25 — the sky dome radius is chosen against the arena

The floor is a couple of hundred metres across, so a dome at six hundred swung
by nearly twenty degrees as a player crossed it — the sky sliding rather than
the machine walking. At fourteen hundred it is a few degrees.

Puff size scales with radius, so pushing it out costs nothing but parallax, and
it is still four orders of magnitude short of straining a float. The retail dome
sits ten million units out, which suits a track renderer written around it and
does not suit this one.

## MESH-26 — the forest grows outwards off the boundary

The trees a machine can hide behind are boxes built from the same panels as the
cover. These are the other hundred and fifty: the game's own tree sprites,
upright and camera-facing, with nothing behind them. They do not collide, do not
block a shot and are not on the ground mesh. They are there so an arena with an
invisible boundary reads as a clearing in a wood rather than a field that stops.

Placement is a hash of the tree's index and the match seed, so nothing is stored
between frames and the same match always grows the same forest.

The draw is squared rather than uniform. A square scatter puts as many trees
five hundred metres away as fifty, which is a thin haze on the horizon and
nothing at the edge — and the edge is the point, since that is where the
invisible wall is. Squaring crowds them against the boundary and thins them
behind.

## MESH-27 — a tracer's streak keeps the weapon's colour

That colour is how a player tells whose fire is crossing the arena. A textured
quad draws the frame's colours and nothing else, so a plasma-skinned streak
would make every machine's beams the same blue. The head is small enough to read
as the glow at the front of the bolt rather than the bolt.

## MESH-28 — a blast opens fast and collapses

There is no alpha in an indexed frame buffer, so size is the only thing carrying
the shape of the blast. The earlier curve grew all the way to full scale at the
end of its life, so a blast covered the most screen on the last frame before
vanishing — which reads as the arena being blanked and restored rather than
something exploding. Peaking a third of the way in and shrinking from there
reads as a burst.

## MESH-29 — debris cools as it falls

The ramp runs from the pale gold at the top of the sky gradient back down
through orange into the deep reds at its zenith. The shared indices are not a
coincidence worth fighting: they are the one contiguous warm ramp the palette
has, they read as heat in either palette, and a particle walking them downwards
is a particle going out.

## MESH-30 — the jetpack flame is two mirrored halves

The fire tiles are drawn leaning one way, so a single one reads as a flame blown
sideways — wrong for something pointing straight down out of a jetpack. Two
halves meeting down the middle, the right-hand copy mirrored, cost one extra
quad and no overlap, so nothing is drawn twice into the same pixels and the
painter's order has nothing to decide.

______________________________________________________________________

## REND-01 — the mode carries its own font

ROLLER's HUD font lives in the retail sprite blocks, which the rest of this mode
deliberately does without: mechs, arena and effects are all generated rather
than loaded. A HUD that needed game data would be the one asset dependency in an
otherwise self-contained mode, so the mode carries a five-by-seven face as a
fallback. Each glyph is seven rows of five bits, most significant bit leftmost.

`minitext.bm` is the small face the race HUD prints speed and gear with and is
what the mode uses when it is present; `font6.bm` is the larger sprite face the
game announces things in, and is what the title and round banners want. The two
are not interchangeable.

Two things about the retail path matter. Glyphs are indexed through
`ascii_conv3` (or `font6_ascii` for the large face), where 255 means "no glyph"
and costs a flat four pixels of advance. And `prt_letter` scales through the
`scr_size` global rather than an argument, pre-multiplying the coordinates it is
handed — so drawing at `iScale` means setting the global and passing coordinates
that have *not* been scaled.

## REND-02 — the camera does not dodge occlusion

It used to: a segment trace to whatever it was looking at, and up to six
three-metre steps upward until the line came clear. That was always eager — it
swung the whole arena for one pillar — and it got much worse once the ground
itself began blocking that trace, because then every hill the player drove
behind heaved the camera into the air.

Virtual-On does not move the camera for this at all. It leaves the camera where
it belongs and turns whatever is in the way transparent, which keeps the frame
still and tells the player exactly what is happening. That wants a renderer that
can blend, so it is not written yet. Until it is, nothing happens, which is
better than the wrong thing happening quickly.

The floor clamp is not this and stays: keeping the camera out of the ground is
not occlusion avoidance.

## REND-03 — the chase rig scales with the machine

The chase is written around a machine fourteen metres tall, which is most of the
roster. The car is a sixth of that and would be a speck under a camera hung
fourteen metres up, so the rig scales.

Not all the way down: a car doing seventy metres a second needs to see further
ahead than two metres of camera height gives it, and the floor clamp is what
stops the view ending up in the bodywork.

## REND-04 — the camera follows the player's heading, not the enemy's bearing

It used to swing onto the bearing to the enemy at every range, so the view
turned when the enemy moved rather than when the player did — and with the
machine no longer squaring itself up outside knife range, the camera pointed
somewhere the machine was not.

## REND-05 — near-plane clipping keeps the polygon a quad

`game_render_quad_world` accepts quads only, so a vertex behind the near plane
is pulled forward along an edge that crosses it rather than the polygon being
split. This is what stops a floor tile the camera is standing on from smearing
across the screen when the rasteriser clamps its z.

## REND-06 — banks are resolved before anything is built

The mech mesh is the first thing to ask which banks are loaded, so answering
with last frame's result left the car in flat paint for its first frame.

The effect bank loads itself, because the first shot fired names it and the draw
path loads whatever a quad names. The car's skin has no such trigger — the mesh
will not name a bank it has been told is missing, and the bank stays missing
because nothing named it — so it is asked for explicitly, and only when there is
something in the fight to wear it.

## REND-07 — how the banks load, and what the loaders get wrong

Every bank is loaded by a routine the game already has; nothing here parses a
`.DRH`. What this owns is the part those routines are careless about.

Each calls `ErrorBoxExit` when its file is missing — taking the process down
rather than returning a failure — so each is probed first, and a bank that is
not there simply never becomes available. This matters most for
`LoadGenericCarTextures`: on a checkout with no retail data it would kill the
process instead of falling back, and falling back is the whole point. Every
effect still carries a palette index, so a mode with no bank draws what it drew
before.

Each uploads through `g_pGameRenderer`, the global the race sets up, so a mode
drawing on its own renderer gets the decompress and the sort but no upload. The
pixels are left in a global either way, so they are handed to the renderer that
is actually drawing.

The engine's own numbering is not exposed past that table: the track bank is
bank 0 while its tile count lives at `num_textures[19]`, and that is not a quirk
worth spreading through the mesh.

## REND-08 — the low byte means different things on different paths

`POLYFLAT` takes its colour from the low byte of the surface flags and routes
anything marked transparent through `shadow_poly`. On that path the low byte is
a shade *level*, not a colour, and `shade_palette` holds only 16 blocks — so the
value is masked. A bad colour is a visible bug; a bad read is not.

On the textured path the low byte is a *tile index*, which is the easiest thing
on it to get wrong: a colour left in those bits names a tile the bank does not
have, the renderer rejects it, and the quad quietly comes out flat.

`PARTIAL_TRANS` is what makes a frame a sprite rather than a black square: on
that path index 0 is skipped instead of written, and every effect frame is drawn
on index 0, with between a third and nine tenths of each tile background.

## REND-09 — POLYTEX derives its own coordinates, so corner order is the API

The legacy path works its texture coordinates out inside `POLYTEX` from the tile
index and the projected polygon; the track renderer passes zeroes on every
vertex and always has.

So the order the four corners arrive in decides how the tile lies on them, and
the arena winds its quads the other way round the face from the track. Nothing
else in the mode noticed: this renderer rejects back faces off the stored normal
rather than the projected winding, so a quad wound backwards still culls, sorts
and fills correctly, and every texture it had worn — grass, tarmac, concrete, a
plasma bolt — was near enough symmetrical to look right mirrored. Put lettering
on one and it reads backwards.

Geometry the mode builds itself is therefore handed over reversed. Geometry out
of the game's own files is not: it arrived already reflected by the frame change
that got it here. See [MESH-12] for the flags that decide the rest.

## REND-10 — the briefing's line budget

The game's smaller video mode gives this a 320x200 buffer, and at 200 pixels
there is room for exactly twenty-five lines. Anything that does not fit is lost
off the bottom, and the bottom is where the exit row lives.

Besides the rows themselves: two lines for the double-height title, one for the
result and a blank after it, a blank either side of the rows, the footer, and
one more as the margin the footer's glyphs need.

The controls used to be printed here, ten lines of them, which is most of why
the rows had nowhere to grow. They are on a page of their own now — still one
keypress away, no longer in the way.

## REND-11 — the sky is DrawHorizon, and the clouds are off

`DrawHorizon` paints the sky, the same routine the race uses: two flat fills
split by a line through the projection, blue above and a haze colour below. The
arena had a nine-band sunset gradient before this, which looked well enough on a
still frame but was the mode inventing a sky the engine already had, and it
could never carry clouds.

What `DrawHorizon` reads, it reads from globals. Most are already written by the
time this runs — `game_render_set_camera` and `set_projection` push `viewx`, the
vk basis, `xbase`, `ybase`, `scr_size` and `VIEWDIST` through for exactly this
kind of legacy path — so what is left is elevation, tilt and colour.

The clouds are disabled. That dome is real geometry: forty quads placed ten
million units out, in the track code's coordinate system where the up axis is Z
rather than Y, submitted through the renderer's cloud subdivision path. Handing
that path an arena camera makes it subdivide quads that size until the frame
stops arriving — a run that takes a fifth of a second takes minutes. Getting
them in wants the dome rebuilt against the arena's own scale and axes, rather
than the basis swapped underneath it.

## REND-12 — why these palette indices

The names live next to the code that uses them; the palette table is where those
indices get colours for the case where no palette has been loaded.

The indices themselves were chosen by matching the intended colours against the
retail palette, so a player with the game data sees roughly the same picture
from their own `PALETTE.PAL` rather than whatever sits at an arbitrary index.
That palette is mostly a grey ramp between 115 and 143 with saturated primaries
higher up, which is why the arena reads as grey structure with coloured tracers.
Some indices are deliberately shared — a tracer and a HUD accent — because the
mode paints into a palette it does not own all of.

The sky bands climb steadily in brightness whether resolved through the retail
palette or the fallback: 221-230 is a dark-to-bright red ramp, 167-171 orange,
204-207 the top of a yellow. **231 is deliberately unused.** It is the obvious
brightest red to finish on, and it is also the low-armour warning: a sky
matching the colour of "you are about to die" hides it.

## REND-13 — the recoloured effect banks

The game's plasma frames are blue and there is only one set, so every machine's
fire came out the same colour. In a crossfire you could not tell whose shot was
whose, which is the one thing a shot has to say.

A tint is built by walking each frame's palette indices onto the nearest colour
the palette has in the wanted hue at the same brightness, and uploading the
result as a bank of its own — so the recolour is real pixels rather than a
shading trick the rasteriser does not have. Index 0 stays index 0: that is the
transparent key and everything about these frames depends on it.

Slots 20 and up are used. The engine's texture-count table only ever speaks for
0, 17, 18 and 19, so the rest are free for the arena to take, and the count is
set alongside the upload.

______________________________________________________________________

## SIM-01 — an empty gauge takes the thrust, not the legs

The gauge gates four things. Only three of them are thrust:

| action               | needs gauge |
| -------------------- | ----------- |
| jump takeoff         | no          |
| hover (holding jump) | yes         |
| dash, air dash       | yes         |
| jump cancel          | no          |

Leaving the ground is the legs' work, so an empty machine still jumps and still
cancels out of the jump. What it loses is the thrust to hang in the air with and
to dash with.

The takeoff used to be gated on the gauge, which meant an empty machine could
not get airborne at all — and so could not jump-cancel either, since the cancel
needs a jump to cancel. The lockout cooldown on spending the last of the gauge
is deliberate and stays; what it should cost is hover and dash, not the ability
to leave the ground.

The cost is charged only when there is gauge to charge. Otherwise a locked
machine mashing jump would spend the recovery it needs in order to unlock, and
never climb back out.

______________________________________________________________________

## ARENA-01 — palette indices are tuned, not derived

ROLLER's flat polygons take a palette index in the low byte of the surface
flags, and that palette comes from the game's own data. Every arena and mech
colour goes through a name defined in `mecha_arena.c` or `mecha_defs.c`, so
retuning against a different palette is a one-file edit. See [REND-12] for how
the indices were chosen.

The meadow takes its greens from 244-255, a black-to-green ramp in the game's
palette, and its browns from 48-63, rather than borrowing tracer colours; stone
stays one of the greys. A field checkered green against grey read as a chess
board, which is the one thing a meadow must not look like.

## ARENA-02 — the projectile trace stride

The stride decides whether a bullet can step over a hillside between two
samples. A metre against hills forty metres wide leaves no gap to step through,
and the fastest shot in the game covers seven metres in a tick, so a segment is
eight samples at worst. The step cap exists only so an absurdly long query
cannot become an unbounded loop.

## ARENA-03 — the ground begins slightly below where it is drawn

The gun car's weapon floats six metres off its right flank, so parked across the
steepest hillside in Coldwater Meadow its muzzle dips about three centimetres
into the slope. Sweeping every machine over every square metre of that arena at
sixteen facings found that in 24 of 2.28 million samples — rare, and unplayable
where it happens, because a shot that begins underground detonates at the
muzzle.

So the ground is treated as beginning a little below where it is drawn. Twenty
centimetres is six times the worst graze measured and small enough to be
invisible: a shot stopping into a slope stops a fifth of a metre late along the
normal, on hills that stand twenty-six metres.

## ARENA-04 — hills are raised by hand, and are meant to be faceted

Pick a middle, a reach and a height, and every grid corner inside comes up by a
cosine of its distance. Angular, because the corners are all the ground has: a
hill built this way is a dozen facets, which is what it should look like.

## ARENA-05 — a ramp is a truncated cone, and every part of that is load-bearing

- **Straight sides** are one constant grade, which is what a ramp is. A smooth
  shoulder launches nothing, because by the time the machine is fast the slope
  has flattened out under it.
- **A flat top** is somewhere to land and fight, and it stops a walker hopping
  the apex — a cone that comes to a point drops out from under anything crossing
  it, boost or no boost.
- **The edge between them** is the lip the launch comes off.

## ARENA-06 — retail tiles do not replace the palette entries

Track-bank tiles are used whenever the retail data is installed. The palette
entries stay as the fallback, so an arena still comes up on a bare checkout and
draws the same checkerboard, only flat.

Each arena takes a different surface so they do not read as one place with the
furniture moved: a yard in tarmac, a field in grass, a plate floor in worn
metal.

## ARENA-07 — the meadow, and why it has no visible walls

Eight sides, hills you can be thrown off, nothing built on it. The ground is not
magnetic, which is the point: boost up one of these and you leave it at the top.

The boundary is still there and still stops a machine, but what is drawn past it
is more forest — ground running out to twice the arena again with trees on it —
so the edge of the fight is a place the fight stops rather than a place the
world does. A wall in a meadow is a fence around a field.

## ARENA-08 — MERIDIAN CROSSING puts a city in the middle of that

The biggest ground the mode has: an octagon four hundred and twenty metres to a
side face, near enough twice the meadow. Almost all of it is meadow — the same
rolling, non-magnetic grass with hills, rocks and a wood, and forest drawn past
the boundary.

What is different is the middle. Three blocks by three of tall building sit at
the centre and nowhere else, so the city is a place you go into rather than a
place the arena is. The streets do not stop at the last building: they run
straight out to the boundary both ways, which is what stops the city reading as
nine boxes dropped on a field.

## ARENA-09 — the tower roof

No walls at all — walk off it and you are falling — with a raised hexagonal
tabletop in the middle and a block in each corner to fight around. The tabletop
is sloped rather than sheer, so it is high ground you take rather than a wall
you go round.

The edge runs a long way down. It is the top of a tower, and a tower that stops
six metres below its own roof is a table.

## ARENA-10 — terrain is a height per grid corner; the tabletop is not

The cell a point falls in is found by index and the height inside it
interpolated between four corners, which is what makes a slope a slope rather
than a staircase. Everything else about the ground — the pit, whether a machine
sticks to it — lives in the cell's surface word, exactly as a track chunk's
does.

The tabletop is answered rather than baked. Hills go into the grid because they
are meant to be lumpy. A tabletop is a made thing with six straight edges, and
rounding those to the nearest grid corner would lose the only thing that says
somebody built it. The ground mesh picks the computed version up for free,
because it samples the same query at every corner it draws.

## ARENA-11 — a platform is a floor from above and nothing from below

Off the edge there is no floor at any height, and underneath it there is none
either. Without that second half, a machine that has fallen past the edge and
drifted back beneath the roof pops up through it — the same mistake as walking
into the side of a box and being teleported onto its roof.

## ARENA-12 — the ground query is what makes terrain stop a bullet

The floor used to be the `y = 0` plane, which was true of the first three arenas
and nothing since. A shot crossing Coldwater Meadow passed clean through every
hill it met, and one fired across Tower Seven went through the tabletop, because
neither is at zero.

The terrain query is what the ground mesh is built from, so asking it here is
what makes the shape you can see the shape that stops a bullet.

## ARENA-13 — grip, in the race game's own fourteen grades

Whiplash keeps a table of surfaces (`loadtrak.c`, `tSurface surface[14]`) and
stores an index into it per track chunk, separately for the centre lane and each
shoulder. What the physics reads off it is `iGripModifier`, running 100, 95, 90,
85, 80, 75, 70, 65, 60, 55, 50, 40, 30, 20 — then adds the engine's own grip
bonus and divides by how wrecked the car is:

```
(modifier + engine.fGripBonus) / (2.5 - health * 1.5)
```

clamped to `fMaxGripLimit`. Only the first term is a property of the ground, so
only that is here: the engine bonus is the machine's own grip figure in the
roster, and damage is accounted for elsewhere.

Written as a fraction of the best surface, so grade zero is 1.0 and costs
nothing. Every track the race game ships is laid at the maximum bar one bonus
track, which is why an arena that says nothing gets the best of it.

## SIM-02 — damage particles come off the machine's own generator

The race game's `dospray()` runs every frame over every car and throws a
particle when a die roll beats the car's health factor — the worse the car, the
more often it lands, so a machine does not switch from clean to smoking, it gets
gradually dirtier. Whiplash has one damage tier; this has two, so a machine that
is merely hurt smokes and one nearly gone burns as well.

Every draw comes off the machine's own RNG, not the world's. Written the other
way first, this took three extra numbers a tick out of the shared stream and
moved a rooftop fight off a cliff. The particles were fine; the fight was simply
no longer the same fight.

The same precedent applies to anything else cosmetic that needs randomness.

## SIM-03 — guard stops melee only

Guard is a posture for answering something that has closed the distance, not a
shield. Standing in it against gunfire has to lose, or the fast boost refill it
already grants would make it the only thing anyone ever does.

The helper returns 1.0 for every case that is not a guarded melee hit, so
callers can multiply unconditionally.

## SIM-04 — a downed machine takes one blow, but a volley is one blow

Three ways to be off the table: destroyed, invulnerable through a rise, or lying
on the floor. The last is the point — a knockdown should be a reprieve, not an
invitation to empty a magazine into something that cannot move.

The reprieve starts on the tick *after* the knockdown, not on the hit, so a
single volley resolves in full. Buckshot is seven projectiles and one trigger
pull; if the first pellet to arrive closed the door on the other six, a shotgun
would do a seventh of its damage exactly when it was working.

## SIM-05 — the lock is live only inside a cone

`mecha_update_target` picks who; this decides whether the lock is live. It holds
while the target sits inside a generous cone of the machine's own heading and
drops once it has been outside for the grace period, at which point the
auto-turn stops following and every weapon fires straight down the barrel.
Boosting or jumping snaps it back from any angle, which is what makes those
worth gauge for reasons other than distance.

It reads `byMove` as movement left it last tick. One tick of lag on a dash that
lasts dozens does not matter, and running before movement is what lets the
facing update act on a fresh lock.

## SIM-06 — the car's steering is Whiplash's, both halves of it

Whiplash works the lock out as `input * (1 + (top - speed) / k)` and then throws
it away entirely below the car's own steering speed limit (`control.c`). Both
halves are here: the lock is widest just off a standstill and narrows as speed
comes up, and a car that is not moving cannot be pointed at all. That second
rule is why the gun car has to keep moving to point at anybody, which is the
whole of how it fights.

It reads the stick as much as the turn axis, because a car has no strafe for the
stick to mean anything else by.

The exact form in `control.c` is `input * (1 + (360 - speed) / 60)`. 360 is that
game's reference speed, so the divisor is a sixth of it and the bonus runs from
seven times the input at a standstill to nothing flat out. Written against the
machine's own top speed that is a gain of six on the slack, which is the same
curve.

**There is no ceiling on any of it.** The yaw is simply accumulated: nothing in
the race game limits how far a car may come round, which is why one can be spun
through a whole circle on the stick in a drift. Grip decides whether the car
goes where its nose has gone, and that is a separate number.

**Reverse flips the steering, and a drift must not count as reverse.** Whiplash
decides this on `fFinalSpeed`, the car's signed speed along its nose, and on a
track that is the only speed it has — position is advanced straight along the
heading, so a Whiplash car cannot travel at an angle to where it points. This
one carries a real velocity vector, and in a drift that vector swings more than
a quarter turn off the nose. A test on the dot product then decided the car was
reversing and flipped the steering, which stopped the slide dead. That was the
"rotation limit": not a clamp anywhere, but the stick fighting the spin halfway
through it.

Reverse is a third of forward top speed and a drift is fast, so the car's own
reverse speed separates the two cleanly.

That alone is not enough, though: it flips on any backwards drift, which is most
of a handbrake turn, so the wheels swap hands halfway through and the car fights
itself. The throttle alone is no better — braking and reversing are the same
lever, and a brake at speed is not reverse. The condition is all three: the
lever down, the car going backwards, and slow enough to be reverse rather than a
slide.

## SIM-07 — hitting a wall

The push the arena applied to get the machine back out is the surface normal,
which is all a bounce needs. At walking pace the machine leans on the wall and
the speed into it is dropped — pressing into a corner should not build up a
shove that fires you out of it later. Carry a boost into the same wall and it
comes off, the way the race game's cars do, and the burst is over.

## SIM-08 — a committed dash can still be steered

Two ways in. Boost again while pushing back against the direction you left on
and the dash restarts the other way — the cancel, and the reason a committed
dash is not a trap. Or let the stick go and tap a new direction: the burst turns
without a second press, which is the crossing step, and the release is the whole
cost of it.

## SIM-09 — drive towards a velocity, split into along and across

The velocity a machine already has is split into the part pointing where it is
being asked to go and the part across that. The first is pushed towards the
speed asked for at the machine's drive rate; the second is bled off at its grip.

That single split is what makes a heavy machine slide out of a direction change
and a light one snap round: the sideways component is the skid, and grip is how
fast it stops being one.

The direction must be unit length, or zero to mean "nothing asked for", in which
case everything is treated as sideways and simply brakes.

## SIM-10 — the drawn attitude is never read back

Nothing in the attitude block is read by movement, collision or the firing
solution. That is deliberate and it is what makes it affordable: a machine can
be squatting, ringing and rattling at once because none of the three has to
agree with the others about anything.

**The input tilt goes opposite ways in the two games**, which is the only
interesting thing about it. A car leans out of the corner because that is what
weight transfer does to a body on springs; a robot leans into it because a
machine that has started moving before it has moved feels quicker to the hands.
Neither is more than a couple of degrees.

**The ground contour is wheels-only.** A car sitting perfectly flat while it
drives up a hill gives away that the hill is a height field rather than a
surface, so the machine asks what the ground does across its own footprint —
fore against aft for the climb, left against right for the traverse. A walking
machine has feet and a gait to put them down with, and tilting the whole of it
would fight both. It is also terrain-only: standing on the roof of a box, the
height field underneath describes ground the machine is nowhere near, and
following it would lean the car over on a flat roof.

## SIM-11 — the ground query asks from the higher of two positions

A platform answers a height query only to something near enough above it; below
the lip it is a wall, not a floor, which is what stops a machine underneath a
roof popping up onto it.

A jump cancel falls at a hundred and twenty metres a second — two metres a tick
against a lip of one and a half — so a cancel from high over Tower Seven stepped
straight past the roof in one tick, was told there was no floor, and fell to its
death through solid ground. Measured: -449 m and dead before, resting on the
tabletop at 9 m after.

Taking the higher of where the feet were and where they have got to means the
query sees the surface the machine was standing over when the tick began.

## SIM-12 — downhill slopes are rolled down, not fallen down

The contact rules only see a machine that has sunk to or below the ground. Going
downhill it never does: the ground drops away faster than one tick of gravity
follows, so the machine is left hanging a fraction of a metre up, falls, lands,
and is hanging again — an invisible staircase.

A machine that was in contact when the tick began and is over ground that has
merely sloped away is put back on it, then falls through the ordinary contact
rules like anything else.

Three conditions stop this gluing a machine to the world: it must have been in
contact already, so nothing in flight is caught; it must not be climbing, so a
launch off a crest is never undone; and the ground must have sloped rather than
ended — past one in one it is a cliff, not a hill, and driving off it should
fly.

## SIM-13 — leaving the ground off a ramp, the way the race game does it

A car in Whiplash is held to the road by the surface being magnetic. Where it is
not, the game compares where the car's own momentum would put it against the
height of the ground under it, and if the ground has dropped away the car is in
the air.

The same rule from the other end: on a surface that does not hold you, the rate
the ground rose under you this tick is a real upward velocity, and when the
slope runs out you keep it. So a machine that walks up a hill is glued to it —
the climb is slow and the threshold sees to that — and one that boosts up the
same hill leaves at the top.

## SIM-14 — two ways to be gone that are not damage

A pit in the race game is a surface like any other: it answers a height query,
it is simply flagged as a pit and not drawn, so a machine standing over one has
fallen *in* rather than fallen through. And below the kill plane there is
nothing at all, which is what becomes of anything that walks off an open arena.

## SIM-15 — one gun, three triggers, one magazine

A machine carrying a single weapon still has all three slots, so it plays and
reads like everything else on the roster. But they are three loads for the same
gun, not three guns, and a magazine that could be stretched by rolling across
the other two triggers would not be a magazine. Every round spent is spent out
of all of them, and they run dry and reload together.

Which makes the choice a real one: nine rounds, each either buckshot, a lance or
a shell, and nothing about picking the third stops the first two costing exactly
as much.

## SIM-16 — shots can shoot each other down

Two shots that meet are worth what they do: within a sixth of each other they
trade, both gone, and outside that the heavier one carries on unchanged. It is
what makes a siege shell worth the wind-up and a spread worth firing at one, and
it is why a wall of fire is a wall rather than a suggestion.

Anything carrying a blast goes off where it was stopped rather than blinking
out, so shooting a bomb down is a decision about *where* it explodes rather than
whether it does.

## SIM-17 — the gun car can run somebody over

It is the only thing that machine has at close quarters — it carries no melee
row at all — so it has to hurt. Charged on the speed the two are closing at
rather than on its own speed: driving alongside somebody is not a ram, and a
head-on is worse than catching them up. Both machines can be doing it at once,
and neither can do it to a friend.

______________________________________________________________________

## DEF-01 — the lock is breakable, and getting it back is harder than keeping it

Weapons aim themselves at whatever is locked, so a lock that can never be lost
means the fight is decided entirely by the feet. Holding it is a skill instead:
it survives while the target is inside a generous cone of the machine's own
heading, and once it has been outside for the grace period it drops — the
auto-turn stops following, shots fire straight down the barrel with no lead, and
missiles launch unguided.

On its own it returns only when the target is well inside the much narrower
reacquire cone. The quick way back is to boost or jump, either of which snaps it
on from any angle — which is what makes those two worth gauge beyond the
distance they cover.

## DEF-02 — guard cuts damage by 85% and stagger by half

It was a crouch, and it still refills the gauge fastest and selects its own row
of weapons. What it adds is a hard answer to being closed on. Melee only, for
the reason in [SIM-03].

Stagger is cut by half rather than by the same 85%, so a guarded blade still
rocks the machine it lands on. Reading the swing should win the exchange
outright; it should not make the swing feel like nothing happened, and leaving
some stagger on is what keeps a blade rush worth committing to even against
someone who saw it coming.

## DEF-03 — the jump cancel is one move in two halves

A jump snaps the lock on, which makes going up the reliable way to find an
opponent who has got behind you. Guard in the air then drops the machine
straight down instead of riding the arc out, and the landing leaves the turn
rate off its leash for a moment — long enough to come down facing the other way.
Up to find them, down to face them.

A cancelled jump does not fall, it is dropped. Forty-six metres a second was a
brisk fall; at a hundred and twenty the machine is simply on the ground, which
is what makes the cancel a way out of an arc rather than a slightly faster way
of finishing it.

See [SIM-01] for what an empty gauge does and does not take away.

## DEF-04 — ground tiles were chosen by measuring the banks

`track1.drh` holds 246 tiles and most are track furniture — kerbs, arrows, lane
markings, a sponsor emblem — none of which survives being tiled across a floor.
What a ground surface needs is uniformity, so the candidates were ranked by the
standard deviation of their luminance and the flattest taken: 54 and 55 are the
same grey a shade apart, 205 and 210 clean grass, 12 and 13 concrete and rust.

A pair has to be neighbours in appearance as well, because the two alternate
across the floor the way the two palette entries do. Pairing plain tarmac with a
lane-marked tile turned the arena into a chessboard instead of a surface.

## DEF-05 — the auto-turn is confined to knife range

It used to run at every range, which quietly took the steering away: there was
no distance at which a player chose where the machine was facing. Confining it
keeps the one place it earns its keep — a melee exchange is too fast to aim by
hand — and gives the rest of the fight back.

The lock is unaffected either way; it still decides whether the weapons lead,
and holding it at range now means actually keeping the enemy in front of you.

A move that re-centres holds the machine on its lock for long enough to complete
the turn and let a shot go, and no longer: the auto-turn coming back on
permanently would undo the point of confining it.

______________________________________________________________________

## TYPE-01 — surface bits are the engine's own values, duplicated

`SURFACE_FLAG_PIT`, `SURFACE_FLAG_SKIP_RENDER` and `SURFACE_FLAG_NON_MAGNETIC`
out of `types.h`, which `mecha_types.h` cannot include because nothing in the
simulation may reach into the engine. `mecha_render.c` includes both and asserts
at compile time that they agree.

## TYPE-02 — silhouette multipliers exist so archetypes read across the arena

Everything the mesh builds is scaled off `fHeight` and `fRadius`, which made
every machine the same shape at a different size — the archetypes existed only
in the stat block. These let a siege platform read as one: heavy shoulders,
thick limbs, an oversized gun in each hand, a head sunk into the chest, against
an interceptor that is all narrow torso and thin legs. Zero means one, so a
machine that never sets them still builds.

## TYPE-03 — the three mass figures are absolute, not multiples of walk speed

The race game's cars do not set their velocity, they drive it: a grip figure
limits how fast sideways motion is corrected, whatever is left decays on its
own, and steering authority falls off as speed rises. The same three ideas are
what make a machine here feel like it has mass rather than a cursor.

`fGrip` kills sideways velocity per second — high is crisp, low slides wide.
`fDriveAccel` is how hard it pushes towards the speed asked for. `fBrake` is how
fast it sheds speed with nothing asked of it.

All three are in metres per second squared and deliberately **not** multiples of
the machine's own walk speed. Scaling them that way normalises out the very
thing they exist to express: every machine then takes the same time to gather
itself, so the interceptor — being simply faster — slides the furthest, and the
siege platform comes out the nimbler of the two.

## TYPE-04 — what the attitude fields are, and where each comes from Whiplash

All of it is cosmetic, all in the shared 14-bit circle, none read back. Whiplash
keeps these in independent pieces and sums them at the last moment in `car.c`;
the same split is kept because they genuinely do not interact.

- **Input tilt.** Whiplash moves it *against* the steering
  (`iRollDynamicOffset`, wound at `iRollResponseRate`, clamped at
  `iMaxRollOffset`) so a car leans out of a corner. Virtual-On's robots lean
  *into* the input. Two degrees either way: you would not name it if you saw it,
  and you would notice if it went.
- **Air pitch.** The nose follows the velocity vector with no ground under it —
  Whiplash derives airborne `nPitch` from `atan2` of vertical against horizontal
  speed, so a car launched off a crest points where it is going.
- **Ground contour.** Pitch and roll of the slope actually stood on, sampled
  across the footprint and eased rather than snapped. See [SIM-10].
- **Landing wobble.** Two amplitudes decaying while a phase runs, giving a
  damped cosine about both axes. Whiplash seeds them from the attitude held at
  the moment of contact, which is why a flat landing barely registers and one
  off a hillside rings.
- **Body shake.** White noise on all three axes, resampled every tick, scaled by
  how hard the machine is working — in the race game, road speed times damage
  divided by the engine's `iStabilityFactor`.

## TYPE-05 — the leg facing is the one piece of animation state the sim owns

The torso holds the aim while the legs follow the line of travel, so a machine
strafing across your guns is walking sideways rather than sliding with its
shoulders square. The sim owns it because it is smoothed over time and the mesh
is built fresh every frame.

`fFightMix` is similar in spirit: how much of a fight the machine thinks it is
in, held while it has a lock or is shooting. Nothing reads it back, so a machine
animating out of a fighting stance has never stopped fighting.

## MESHH-01 — the effect bank's frames, and what doubles as what

Frames 8..12 are the sky's cloud puffs — `horizon.c` picks one of those five for
every quad of its dome, and so does this mode. They double as the glow on a
plasma bolt, because the bank has no bolt art of its own: at bolt size a soft
blue puff reads as plasma. 0 and 21..23 are smoke, 1..3 the start lights, 4..7
flame, 13..20 the blast.

## MESHH-02 — the quad buffer is caller-owned and fixed

Nothing in the mode allocates, so a frame that would overflow stops adding
geometry rather than growing or crashing. `iDropped` records how much was lost
so a debug overlay can say so.

The mesh layer never sees a texture either: it names a bank and a tile and the
renderer resolves them, which is what keeps the file free of the engine.
Anything unresolvable falls back to `byPalette`, so the same mesh works with or
without the retail data.

______________________________________________________________________

## MODE-01 — the palette dance on entry and exit

Everything the arena draws is generated, but the frame is still an indexed
buffer presented through `pal_addr`, and `pal_addr` is only filled in by the
states that load the retail data. Coming straight in on `--arena` skips all of
those, so without the mode installing its own the geometry rasterises correctly
and then presents as a black screen.

The game's own palette is loaded first when it is installed. The fallback table
defines about thirty indices and fills the rest with one neutral grey, which is
fine for geometry the mode colours itself and wrong for anything out of the
retail banks — those tiles and frames are drawn in the retail palette's indices,
so resolving them through the fallback turns a tarmac surface into noise.

**Two ownership traps, both of which crashed the process.**

`setpal` owns `pal_addr`: it frees whatever was there, loads the file, and
points `pal_addr` and `pal_selector` at its own buffer. The mode used to repoint
`pal_addr` at the static `palette[]` array afterwards, on the strength of a note
in the GPU renderer saying `setpal` leaves it alone — true of the original, not
of this one. The cost was not a wrong colour: the loaded buffer leaked, and the
next `setpal` anybody called (the main menu's, on the way out) took the static
array's address to `free()` and aborted. That was the crash on "exit to
whiplash".

Presentation reads `pal_addr`, so the mode's own table has to go there, and that
table is static. The selector is how the engine says whose memory this is:
`setpal` frees `pal_addr` only when the selector is non-negative, so marking it
-1 while the arena's table is installed makes the static safe to leave there.
Both go back on the way out.

## MODE-02 — the renderer is created if absent and never torn down

`g_pGameRenderer` is created by `play_game_init()`, which only runs once a race
starts. Coming in on `--arena` leaves it NULL, and `game_render_get_mode()`
dereferences it without a guard, so the mode stands one up itself the way
`play_game_init` does.

It is not torn down on the way out. Doing so nulled `g_pGameRenderer`, which is
the renderer the menus and the race then reach for, so leaving the arena crashed
the moment anything else tried to draw. It is the same renderer `play_game_init`
would have built; handing it on is the point of having built it.

## MODE-03 — the fixed tick, and the catch-up cap

The simulation runs at a fixed 60 Hz whatever the display does, so a match plays
identically anywhere and stays reproducible from its seed. A frame that took too
long catches up over a few ticks and no further: without the cap, one long stall
— a window drag, a breakpoint — is paid back as a burst of simulation the player
cannot react to.

______________________________________________________________________

## TEST-01 — what the headless render test is for

`mecha_sim_test.c` covers the simulation, which needs nothing but libc. This
covers the other half: a real `GameRenderer` in software mode with no GPU device
and no window, rendering arena frames into an indexed buffer, and asserting that
geometry, effects and HUD all reach pixels. That is the part no unit test and no
compile check can speak for.

Given an output directory it writes the frames as indexed PNGs so the layout can
be looked at rather than only asserted about. They are dumped through the
palette the frame was actually drawn with, when there is one: dumping through
the mode's fallback regardless is what made these previews lie, since retail
tiles and effect frames are drawn in the retail palette's indices and showed as
noise for surfaces that were fine on screen.

## TEST-02 — assertions that must be one-directional

Two places where the obvious two-way assertion is wrong:

- **Palette coverage** is checked forwards, from the constants, not backwards
  from the frame. `shadow_poly` emits indices out of the shade table that the
  mode never chose, so "everything on screen is one of ours" is false and
  asserting it only produces failures.
- **The HUD's colours** are only the mode's while the mode is choosing all of
  them. The retail font brings its own indices, so with it loaded that assertion
  says nothing — and would amount to asserting the font failed to load.

## TEST-03 — the blast is measured differently on each path

Drawn from the game's own texture bank, the blast paints none of the flat path's
palette index, so a count of that index is legitimately zero and the size bound
belongs to the other path. Without the bank — a checkout with no retail data,
which is how CI runs — the flat particles are what is on screen and their size
is what is worth pinning.

What proves the bank frames reached the screen is the opposite test: the bank's
tiles are drawn in retail palette indices, mostly ones this mode never paints
with, so pixels the mode's own palette does not define can only have come from a
sprite. It is also why the dumped PNGs look empty on that path — written through
the fallback palette, those indices resolve to neutral fill.

The flat path's colour is not required to vanish either: the machine's visor is
painted in it, so counting that index was only ever an upper bound on blast
size.

The bound itself: the explosion is an opaque billboard whose scale is a
half-extent, so an over-large figure paints a slab across the middle of the
screen on the frame the player most needs to read. Measured against a recorded
match, the original covered 17% of the play area at its widest.

## TEST-04 — the numbered panel render

The plan's fifty polygons are the first fifty quads the mesh puts out, one from
each, so a quad's place in the list is the polygon's number.

Only panels facing the camera are numbered, or the far side of the car writes
over the near side. Which those are is read off the sign of the projected screen
area: all fifty share the plan's winding, so those turned towards the camera
come out one sign and those turned away the other. That needs no view on how the
normals ended up pointing in this frame.

Nearest panel wins the space. Without that the roof and the tail, whose middles
project into the same corner of the screen as the windscreen, write their
numbers over the panels being asked about.

Shots are named for what the camera looks at, which is the far side of the car
from where it stands: the nose points +Z, so the camera out at +Z sees the
front. Flanks are named for the axis they face — which is the driver's right is
not something the geometry says.

## TEST-05 — the briefing has to fit in 320x200

The footer is the last thing drawn, so anything running off the bottom took it
first, and the exit row sits just above it. Twenty-five lines of this font is
the entire buffer. See [REND-10].

## TEST-06 — what the AI duel probes can and cannot assert

**Lock-held fraction is bounded loosely, on purpose.** Both halves have to be
true: the pilot must lose a lock sometimes, or the mechanic does not exist in
its hands and it is quietly privileged over the player; and it must hold one for
most of a fight, or it has no idea how to fight and the skill levels measure
noise. Everything that fights at range holds a lock better than nine tenths of a
fight. Kira sits near two thirds and belongs there — the close quarters machine,
spending the fight at the distance where anything moving sideways leaves the
cone. Asserting the rangefighters' figure would assert that every machine fights
the same way.

**The stand-in player has to steer.** It did not used to: a locked machine
squared itself up at any range for free. With the auto-turn confined to knife
range, a scripted opponent that never touches the stick spins away from the
fight, and the probe then measures how often the computer pilot wandered into
the fixed cone of someone who cannot turn — which ranks a decisive pilot as the
one that takes the most fire.

**Damage absorbed is deliberately not asserted on.** It reads as a skill measure
and is not one: what a pilot takes depends on how long it leaves its target
alive, so a better pilot ending rounds faster cuts its exposure and a worse one
wandering out of the fight cuts its exposure too — the two ends meet in the
middle. Measured across twelve duels the three come out within a few per cent in
no reliable order. An assertion that passes by one per cent is a future failure.
Still printed, because it is worth seeing.

**Recent-shove memory, not instantaneous stagger.** Asking whether stagger was
above zero on the exact tick a machine crossed the line is a different question:
stagger bleeds off at fifty-five a second, so a machine hit hard at the far end
of a slide arrives with none left and books itself down as having strolled. Two
of six seeds did exactly that, each after being shot the whole way across the
roof.

**The rooftop bound is a rate, not zero.** See [AI-05] for the three states in
which the pilot has no steering left to decline anything with.

## TEST-07 — measure along the motion, not along a world axis

A machine faces whatever it has locked, so "forward" is wherever the fight put
it. Measuring against +Z reported zero for all three machines and looked for a
moment like the physics had stopped working.

The two mass levers are measured separately. Grip decides how much of the old
direction survives being asked for a new one, so it is measured by turning
*across* the motion — never by reversing along it, where the sideways component
is zero and grip is never consulted. Drive acceleration decides how long obeying
takes, and reversing is what measures that.

Orderings rather than figures, since the walk speeds these play out at move
whenever the roster is tuned.

## TEST-08 — the inward-facing car body is the check on the axis negation

The race game's frame is right-handed and this one is not, so swapping the three
axes without negating one builds the car's mirror image: same silhouette, wheel
arches and exhausts and both flanks of the livery on the wrong sides. Negating
the lateral axis puts it right, and a reflection reverses a winding — so a
correctly reflected body is one whose panels all face inwards. Drop the negation
and all fifty turn round, which is what this catches, because nothing about the
car's outline would. See [MESH-09].

Silhouette is asserted as an aspect ratio rather than an absolute size, because
size alone is not silhouette: a machine that is merely bigger still reads as the
same machine. See [TYPE-02].

## SIM-18 — a car rolls off a cambered launch, and may land on its roof

Whiplash's own mechanic, in three parts (`control.c`):

- **At launch** (6473): `iRollMomentum += chunk.iRoll * fFinalSpeed / 720`. 360
  is that game's reference speed, so at full speed the momentum is half the
  camber per tick at 36 Hz. The same rotation per second at 60 Hz is three
  tenths of the camber against the machine's own top speed, which is
  `MECHA_CAMBER_SPIN_GAIN`.
- **In the air** (2501): `nRoll += iRollMomentum` every tick.
- **At landing** (3216): roll inside `4096..12288` — a quarter turn either side
  of level — is an ordinary touchdown and the roll is zeroed. Anything else sets
  `iStunned = -1`, zeroes the steering and parks the car at `0x2000`. Here that
  is a knockdown.

The arena has no track chunks, so the camber is the ground-contour roll already
sampled under the wheels. The spin is kept up to date while the wheels are down
rather than computed at the moment of launch, so what carries into the air is
the figure from the surface actually left.

The landing is judged on the tick the wheels touch, not off `byMove`: the
wheeled path sets the car back to `MECHA_MOVE_STAND` before the shared landing
code runs, so there is no JUMP state left to key on by then.

A car that lands upside down keeps what it arrived with and slides on its roof.
Zeroing the velocity made it stop dead, which reads as hitting a wall rather
than going over.

## AI-08 — the pilot presses forward and takes the high ground

Three changes, all because the pilots read as hiding:

- **The station-keeping band is narrow and sits inside the preferred range**,
  and its neutral case walks forward rather than holding position. A pilot
  parked at exactly the range its weapon likes never arrives, and never makes
  the other one move.
- **The guard-and-refill clause needs the gauge to be nearly spent**, not merely
  low. At a third of a gauge it fired constantly, which is what put these pilots
  behind a box for most of a fight.
- **Height is taken on purpose rather than at random.** A box or a building
  answers the ground query at its roof, so something to stand on is ground ahead
  that is well above the ground here and within jumping reach. Looking along the
  line to the target means the thing it climbs is the thing between them, which
  is the one worth being on top of.

The lock-held assertion in the tests moved with this: a pilot that presses
forward keeps the enemy in front of it, so an archetype that never breaks lock
is doing its job. Breaking lock is now asserted across the roster rather than
per machine. [TEST-06]

## SIM-19 — a car in the air bounces off what it hits

Whiplash reflects the approach speed on contact, charges damage for it at
`0.005` per unit, and negates `iRollMomentum` so the spin turns the other way
(`control.c`, the airborne wall cases). A car that clips something mid-flight
arrives somewhere else spinning the other way rather than stopping dead against
it.

On the ground the older rule still holds: walking pace leans on the wall, a
boost comes off it. [SIM-07]

## SIM-20 — a spread is a packed cone, not a fan

Shots laid out along one axis miss above and below whatever they are pointed at,
and cover ground either side that nothing is standing on. Pellets go on a
sunflower spiral instead — a golden angle apart, at a radius growing as the
square root of the index — which fills the circle evenly and puts the first one
straight down the middle.

The half-angle is unchanged; only the arrangement inside it is.

## SIM-21 — the body turns to aim, the travel stays the player's

The stick is body-relative. Firing off a boost or out of the air swings the
machine onto its lock, so a machine crossing in front of its enemy and pulling a
trigger had "left" quietly become a different direction in the world — and since
the airborne and walking velocities are driven straight from the stick, the
whole burst came round with the shoulders.

The stick is now read against `iStickYaw`, the heading the player last chose. It
tracks the facing normally and is held while a recentre is swinging the body.
The body turns to aim; the travel is the player's.

Two things came out of this that were wrong before:

- **The recentre was only counted down when the machine had a lock.** A jump
  sets it regardless, so a machine with nothing locked carried it for the rest
  of the round — which, among other things, kept the air dash at a quarter of
  its speed and let it sink five metres over a burst. It now runs down
  unconditionally.
- **A melee swing still drags the machine into it**, deliberately: the lunge
  *is* the attack, and it sets the dash direction from the swing. That is the
  one case where firing moves the travel.

## REND-14 — the close camera pivots on the machine, not on its feet

At knife range the camera swings round to look along the lock. Orbiting the
machine's origin puts the pivot at its feet, so the body is carried across the
frame and tipped as the camera comes round. Pivoting on the middle of the
machine keeps it where it is on screen and turns it in place.

The usual camera lift is scaled back once the pivot has moved up, or the view
ends up looking down on the fight from the height of two offsets stacked.

## DEF-06 — paint schemes, named for Whiplash's makes

The names are the race game's own, out of `CompanyNames` in `carplans.c`. The
colours are not: nothing in the retail data carries a flat colour per car —
`tCarDesign` is geometry, and `car_flat_remap` is a mirror remap for the
advanced car set. So the schemes are index pairs picked out of the same palette
ramps the roster's own machines already use, which keeps a repainted machine
looking like it belongs in the same arena.

A scheme replaces body, trim and joint. It never replaces the glow index: that
colour is how a player reads whose fire is crossing the arena, and a repaint
that changed it would undo [REND-13].

Scheme zero is the machine's own paint, and `mecha_scheme_get` returns NULL for
it so the mesh falls through to the definition.

## MODE-04 — survival fills the arena

A duel is two machines; survival adds one of everything the roster has, over and
over, until the world is full. Measured on MERIDIAN CROSSING, sixteen machines
cost about 140 ms of simulation for a minute of fighting — a fifth of a per cent
of realtime — and fight down to one or two survivors inside ninety seconds
rather than stalemating.

What it does cost is geometry. Sixteen machines peak near 4,900 quads against
the old 4,096 cap, which silently dropped nineteen thousand quads over a minute
— a machine that stops being drawn because the buffer filled is a machine the
player cannot see coming. The cap is 8,192 now, and the test asserts nothing is
dropped rather than just that it runs.

Everyone is given their own team. The simulation only ever asks whether two
machines share a team, so distinct teams is the whole of a free-for-all.

## MODE-05 — spectator flies ROLLER's own free camera

With no machine of the player's own there is nothing to chase, so the arena
borrows the track's noclip camera: the same mouse look, the same WASD, the same
speed multipliers, driven through `noclip_camera_update`.

It keeps its state in the track frame, where **Z is up**, and the arena is Y-up
— so the two are mapped rather than shared. Position `(x, y, z)` there is
`(x, z, y)` here. For the heading: the arena's forward is
`(sinYaw·cosPitch, sinPitch, cosYaw·cosPitch)` and noclip's, mapped into the
arena's axes, is `(cosYaw·cosPitch, sinPitch, sinYaw·cosPitch)` — so a quarter
turn separates the two yaws and the pitches are the same.

The arena draws through its own renderer and never reads the view globals
`noclip_camera_apply` publishes, which is why `view.c` grew a place/get pair
rather than the mode reading `worldx`.

The camera grabs the mouse while it runs, so leaving a match releases it and
resets `g_bNoclip`, or the briefing has no pointer.

## MODE-06 — the arena has to claim TAB back off the engine

`roller.c`'s event loop binds TAB to the renderer toggle and SHIFT+TAB to split
screen, both of which call `game_render_set_mode` and save the input config. The
arena uses TAB for the target cycle, and `mecha_mode_enter` forces
`GAME_RENDER_SOFTWARE` because every arena frame is rasterised through
`screen_pointer`, `winx/winy/winw/winh` and `scrbuf` — the globals
`mecha_render_frame` writes before it draws. Pressing TAB in a match therefore
swapped the renderer out from under the mode mid-frame and took the game down
with it.

Both handlers now skip while `eFrontendCurrentState` is `eFRONTEND_STATE_ARENA`,
which is the shape roller.c already uses for state-specific behaviour (the
background FPS cap tests `eFRONTEND_STATE_PAUSE_OVERLAY` the same way). The
event still reaches `InputHandleEvent` first, so `mecha_key(WHIP_SCANCODE_TAB)`
sees it.

Why it looked like a survival-only bug: with two machines in the arena the
target cycle is a no-op, so a duel gives nobody any reason to press TAB.
Survival is simply the first mode where the key gets used.

## MODE-07 — a spectator still needs frames drawn

`mecha_mode_draw` returned early when there was no player mech, so a spectated
match presented nothing at all: no `game_render_begin_frame`, no `end_frame`,
and the last briefing frame left on screen. The simulation was running the whole
time, which is what made it read as a lock-up rather than a blank screen.

`mecha_render_frame` already takes a negative view mech: the scene ignores it
outright and `mecha_render_hud` returns before touching `aMechs`, so the world
draws and the HUD simply is not there. Nothing else was needed.

The other half of the same bug was the seat itself. Spectating used to leave the
player's slot empty, so a spectated duel had one machine in it — and
`mecha_check_round_end` only ends a round when more than one team was present,
so that match could never finish. The seat is now filled with the player's
chosen machine under a computer pilot, which is also what makes the COLOURS row
still mean something from the free camera.

## SIM-22 — computer pilots paint themselves off the match seed

Sixteen machines in one paint is unreadable, so every machine that is not under
a player's hands takes a scheme of its own in `mecha_sim_add_mech`.

The draw uses a `tMechaRng` seeded on `uiSeed ^ (slot + 1) * 0x9E3779B9` rather
than `pWorld->rng`. The world's stream decides how the fight goes, and a match
has to replay exactly from its seed: taking paint draws out of it would mean
adding a machine — or changing how many colours exist — silently changed the
fight. A private RNG keyed on the same seed gives colours that are stable for a
given match and independent of everything else.

Scheme 0 is the machine's own palette (`mecha_scheme_get(0)` is NULL), so it is
one of the outcomes rather than a special case. The briefing's own choice still
wins for the player's seat: the mode writes `byScheme` after adding.

## SIM-23 — both triggers together, within a window

Left and right together is the centre weapon. On a pad those are analogue
triggers and they never break their thresholds on the same tick, so the
same-tick test made the centre weapon effectively unreachable.

An outer press now waits `MECHA_FIRE_PAIR_TICKS` (4, so 67 ms) for its partner;
if the partner arrives the centre weapon fires and both outer presses are
dropped, and if it does not the press fires as the outer weapon it was.
`byPairMask` is 1 for left and 2 for right, and 3 is the pair.

Three things the shape of it is protecting against:

- **The window starts once and is never extended.** Pumping one trigger faster
  than the window is long would otherwise hold its own shot forever, since each
  press would restart the wait.
- **It only runs when the centre weapon could actually fire.** No ammo, mid
  reload, still recovering, or no centre weapon at all, and the outer press goes
  off immediately — the 67 ms is only spent when it could buy something. If the
  centre goes away mid-wait, whatever is held back fires at once.
- **Only for a machine under a player's hands.** The computer fires one slot per
  tick and picks each deliberately; pairing its shots turned two aimed outer
  shots into a centre shot it never asked for, which measurably moved the skill
  ladder.

## DEF-07 — the KLR mortar, faster but still a mortar

The shell was 92 m/s under 32 m/s² of its own gravity. `mecha_arc_pitch` solves
the launch angle for whatever speed the weapon has, so raising speed alone keeps
it on target — but it also flattens the arc, and the arc is the entire point of
a weapon that does not need line of sight.

Raising both together keeps the shape and buys the speed: 140 m/s under 75 m/s².
At 60 m the shell now flies 0.43 s instead of 0.66 s and still apexes at the
same 1.7 m, and the maximum range works out at 261 m, which is the lock range
(260 m) it has to cover.

The sim test that read `lance > shell * 4` was asserting a ratio rather than the
intent; it now asserts the shell stays the slowest of the car's three weapons,
which is the thing that must never stop being true.

## SND-01 — the arena's sound reads the world, it is not told about it

`mecha_sim.c` has no idea sound exists, and that is deliberate: the sim is the
SDL-free half and `tests/mecha_sim_test.c` links it on its own. So
`mecha_sound.c` works out what to play by looking at the world each frame rather
than by the sim calling it.

What it watches:

- **Engine and skid** are loops, retuned every frame. There is nothing to
  detect: a machine that is active has an engine.
- **Blasts** are effect slots going from empty to full. `bActive` shadowed per
  slot catches every explosion born since the last frame, however many ticks the
  frame ran — which matters, because the mode runs catch-up ticks and an event
  flag set during one of them would otherwise be missed.
- **Collisions** are `iRamCooldown` going up, which is the sim saying a machine
  just ran into something.
- **Landings** are the airborne flag falling. The fall speed is read a frame
  early: by the time the wheels are down it has already been spent.

The alternative was an event mask on the mech that the sim sets and the sound
layer clears. It would be exact, but it puts a field that only exists for sound
into the structure the simulation is built on, and it has to survive being
consumed by nobody when sound is off. Shadow state in the sound layer costs one
array and nothing anywhere else.

Nothing here needs a sound card: `loopsample`, `pannedsample` and `loadasample`
all check `soundon` and `SamplePtr[]`, so with no device and no FATDATA every
call is a no-op.

## SND-02 — pan is the mixer's convention, not a guess

`DIGISetPanLocation` computes `iPan / 0x8000 - 1` and hands that to the mixer,
where `digi_pan` is documented as -1.0 full left to +1.0 full right. So 0 is
hard left, 0x8000 is centre, 0xFFFF is hard right.

That sign is worth the trouble of checking: getting it backwards puts every
machine on the wrong side of the player, and it sounds plausible either way.
Whiplash's own expression is `(1 - sin(getangle(...))) * 32768`, but
`getangle(x, y)` is `atan2(y, x)` — the second argument drives the sine — while
`mecha_atan2_angle(x, z)` is `atan2(x, z)`, where the first does. The two
conventions cancel the minus sign, so the arena's version is
`(1 + sin(...)) * 32768` and a machine off the camera's right pans right.
Measured, not reasoned: a probe walked a source around the camera and read the
numbers back.

Distance attenuation is Whiplash's constant unchanged,
`65536000 / (d^2 + 65536000)`. In arena units that is half volume at 32 m, which
suits an arena about as well as it suited a track.

## SND-03 — an engine is a pitched loop, and that is what makes a servo too

Whiplash does not synthesise an engine. `enginesound()` plays one looped sample
and rewrites its pitch and volume every frame:

- pitch is `fRPMRatio * 100000 + 8192`, plus a wheelspin term, plus
  `tsin[iEngineVibrateOffset] * (1 - health) * 10000` — which is why a damaged
  car sounds rough, the vibration is a pitch wobble
- volume is `258 * EngineVolume` scaled by engine state, then by distance
- the whole thing is multiplied by a doppler factor of
  `(listener + c) / (c - source)`

The arena uses the same machinery: `MECHA_SND_PITCH_BASE` is Whiplash's 8192,
and the span a machine rides up is its own speed over its walk speed. A walker
gets a narrower span (`MECHA_SND_SERVO_SPAN`) than a car, which is the whole
difference between a servo humming and an engine revving — the same sample, a
different slice of pitch.

That is the lever for weapon and servo sounds later: one sample, and the pitch
says what it is. Nothing here needs a new asset.

Skid follows the same idea. Whiplash decides a car is sliding by comparing the
steered yaw against the one the car ended up with; the arena compares the
machine's facing against the direction it is actually travelling, and folds the
backwards half of the circle away so that reversing is not sliding.

## AI-09 — going round what is in the way

Neither pilot could see an obstacle. The footing checks knew about pits and
about the edge of an open arena, and nothing else, so a car drove into the side
of a building and parked there for the rest of the round, and a walker stood
against it pressing forward. Measured on MERIDIAN CROSSING's middle block, with
the enemy directly behind it: the car travelled 0 m sideways in fourteen
seconds, the walker 24 m and never round.

The fix is a fan. `mecha_ai_detour` traces along the direction the pilot wanted,
and if that is blocked it tries bearings either side -- 17 degrees apart, up to
seven of them, nearest side first -- and takes the first that is clear. The car
steers for that bearing instead of for the enemy; the walker puts it in as its
stick and spends gauge on it. Both come round the same block in under seven
seconds now.

Two things that had to be right:

- **A wall is not a step.** `mecha_arena_ground_height` hides any box whose roof
  is more than `MECHA_ARENA_STEP_UP` above the feet, which is exactly the tall
  ones, so a hundred-metre building reads through it as flat ground. A pilot
  asking the ground query whether it could climb the thing in front of it was
  told yes about a tower block. Two traces at two heights -- the body, and a
  jump higher -- is what actually separates a crate from a building.
- **The detour is decided before the guard.** The pilot used to sit down and
  refill when it had no line and no gauge, which on the far side of a building
  is most of the time. Finding a way round is now a reason not to.

## AI-10 — the pilot takes the crossing step

Watari-dashing is already in the simulation: let the stick go mid-burst and put
it down somewhere else, and the burst starts again the new way rather than
limping out the old one [SIM-08]. The pilot never used it, because it held one
direction for the whole burst.

It now checks, last of all, whether what it wants is far enough off what it
launched with to be worth turning -- and if it is, it lets the stick go for one
tick, because that is what the machine is waiting to see, then puts it down the
new way. Last, because the detour, the evasion and the lock may all still change
its mind about where it is going.

What this buys is the thing it is for: leaving cover on one heading and arriving
on another. The detour above takes the pilot round the corner of a building; the
crossing step is what lets it cut back at the enemy without stopping first.

## AI-11 — a drop is a drop however the arena makes one

`mecha_ai_footing_clear` tested two things: a pit flag, and whether the point is
inside an open arena's boundary. Ground that simply falls away is neither -- the
sides of a causeway are inside the boundary and carry no flag -- so the pilots
walked off them. It also explains the rooftop walk-off in the city arena: the
roof of a building is inside the boundary too.

It now also refuses ground more than `MECHA_AI_FOOTING_DROP` (25 m) below where
the machine is standing. Hills and kerbs are well inside that; a causeway edge,
a roof and a hole are not. On the causeway map it cut the machines lost over the
side in the first minute of a sixteen-way from ten to four.

## ARENA-14 — where machines start when the ground is not a square

The spawn ring is a circle of `fHalfExtent * 0.62`, which is right for every
arena whose floor fills its own boundary. Two lanes with a hole down the middle
is not one of those: half the ring is over the hole.

`bySpawnShape` picks the rule. `MECHA_SPAWN_LANES` spreads the slots along the
long axis and alternates which lane each one is on, so a duel opens at opposite
ends on opposite sides and a sixteen-way fills both lanes rather than the drop
between them. The facing is worked out from where the machine actually ended up
rather than from the ring angle, which is only the same thing on a circle.

The spawn line also has to clear the buildings: at 120 m it put machines inside
the keep's flank wall, which the mesh test caught as quads facing into a box.
104 m stands them on the approach instead.

## ARENA-15 — a hole you fall into, not a hole that deletes you

Whiplash makes a hole in a track by flagging the surface, and the arena
inherited it: `MECHA_SURF_PIT` kills a machine the moment its feet are on a pit
cell. That reads as being deleted. It is also why a hole made that way felt
wrong -- there is no fall, no tumble, and no moment of knowing it has happened.

The causeway map makes holes out of ground instead. `mecha_arena_void` drops
every node far below the arena, and the ground it actually has is painted back
over that a piece at a time with `mecha_arena_lane` and `mecha_arena_pad`.
Anything not painted is a hole, and a machine that goes into one falls --
measured at 35 ticks of falling before the kill plane takes it, against zero for
a pit.

The nodes either side of an edge are one cell apart, so with 40 cells over 400 m
the drop is 260 m over 10 m of ground: a cliff, not a slope.

## ARENA-16 — FACING WORLDS

After the Unreal Tournament map, built from a model of it the player supplied.
Two keeps at the ends of two causeways, with a hole between the causeways on
each side of the middle.

- Seven hundred metres of arena, the keeps 504 m apart, at 6 cm to the unit.
- The lanes are about 30 m wide and they bow: out to 47 m off the axis a quarter
  of the way along, back in to 26 m at the middle, where they all but touch.
  That pinch is the one crossing between them.
- The bases sit at the low ends and the lanes climb 32 m to the middle, so
  leaving a base is uphill and falling back to it is downhill.
- A keep on each base, 90 m square, walls 34 m high. Four walls with a pier in
  the middle of the one facing the causeway, so there are two doorways and each
  opens onto a lane. A single gate on the centreline opened onto the hole
  between them and half the field walked into it inside ten seconds. Open to the
  sky, because a box here is solid from the ground up and a roof would be a lid
  with nothing able to get under it.
- Nothing out on the run. The original has no cover there either -- what it has
  is a crest you cannot see over -- and a block standing near a lane's edge puts
  a wall of terrain quads inside its own footprint, which is a mesh with no
  right answer.

Machines start inside the keeps, which is how the original starts a match.

## ARENA-17 — reading the model, and what it says

The model is Z-up, and the first reading took Y for up. Everything followed from
that: the causeway came out as one bowed strip rather than two, the climb came
out as a W, and the plan was measured across the map's height instead of its
width.

What settles it is the area of the flat faces. Sorting every triangle by which
axis its normal points along and totalling the area, +Z has the most and -Z the
least -- floors facing up, underside cut away. Y-up would have put nearly equal
area on +Y and -Y, which is what walls do, not floors.

Read the right way up, the arena is built from a station every 15 m along the
run. Each station carries where the centre of each lane sits, how wide it is
there, and how high, straight off the model; `mecha_arena_lane` interpolates all
three between stations, so a run of them follows the curve instead of stepping
along it. The table is in `mecha_arena.c` beside the arena it builds.

Two things the measurements do not give and the arena has to:

- **The crossing.** Where the lanes pinch, the model leaves six metres between
  them, which is narrower than the machines that have to use it. It is widened
  to something two of them can pass on: the crossing is the only way from one
  lane to the other, and one nobody can take is a hole with extra steps.
- **The last stretch.** The measurements stop 24 m short of the bases, because
  that is where the lanes merge into them and the sampling can no longer tell
  one from the other. The arena runs the end station out to the base itself.

## ARENA-18 — a box knows what it is standing on

`mecha_mesh_arena` has always drawn a box up from the terrain under its centre.
`mecha_arena_ground_height` read the same `fHeight` as an absolute Y. On level
ground those agree, which is why it never mattered; on a slope the drawn box and
the solid box are in different places.

It showed up as soon as cover stood on a climbing causeway: passing the
collision an absolute height made the mesh draw the box at the terrain height
twice over.

`tMechaObstacle` now carries `fBaseY`, filled in at the end of
`mecha_arena_init` where the ground is finished, and the mesh, the ground query,
the cylinder push and the segment trace all read it. Box heights are plain
heights above their own ground again, everywhere.

## SIM-24 — a spawn stands on the ground, not under it

`mecha_reset_round` asked `mecha_arena_ground_height` for the spawn height with
the feet at zero. On an open arena that is the query for "am I under this
platform", and any ground above zero answers void -- so on the first arena with
raised ground, every machine was placed at -4000 m and fell out of the world
before the round started. It asks `mecha_arena_terrain_height` now, which is the
ground itself with nothing standing on it.
