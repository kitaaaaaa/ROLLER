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
