# Arena mode: next set of mechanics

Notes written at the end of a session so the next one starts from here rather
than from scratch. Nothing below is implemented yet.

These are genre mechanics -- the shape of how an arcade twin-stick mecha game
handles locking, boosting and landing. As with the rest of the mode, the
implementation, the figures and the roster stay original to this project.

## 1. Breakable lock-on

Today the lock is absolute: `mecha_sim_fire` reads `pMech->iTargetIdx` and
aims at it with velocity lead regardless of where the mech is pointing. The
target is never lost.

Wanted: the lock holds only while the enemy is inside a reticle cone around
the mech's own facing. Outside it, tracking degrades and then breaks, and both
the weapons and the mech's own auto-turn stop following. Boosting and jumping
re-acquire immediately from any angle.

- The hook already exists. `mecha_sim_fire` has an `if (pTarget)` branch; with
  no target it falls back to firing along `iFacing` / `iAimPitch` with no
  lead, which is exactly the broken-lock behaviour. Gate that branch on lock
  state rather than on target validity.
- Needs a lock state on the mech (holding / slipping / broken) plus the ticks
  it has been out of cone, so breaking is not instantaneous on a stray frame.
- `mecha_hud_reticle` in `mecha_render.c` has to show the three states, or the
  rule is invisible and reads as the game misfiring.
- Projectiles already in flight keep the target they launched with
  (`pShot->iTarget`, used by `mecha_home_projectile`). Only new shots should
  care about the lock state.

## 2. Boost as a committed line

Wanted: a boost carries the mech in a straight line for its duration, a weapon
can be fired during it without cancelling it, and lock is acquired from any
angle while it lasts.

- Most of this exists. `pMech->fDashDirX/fDashDirZ` are latched when the dash
  starts and `iDashTicks` runs it out, so the straight line is already there.
- What is new is the lock override during the dash, and confirming that firing
  does not cut the dash short. `MECHA_STANCE_DASH` already selects a separate
  weapon per slot, so the weapon side of it is wired.

## 3. Jump cancel

Wanted: jumping acquires the lock; pressing guard while airborne drops the
mech out of the air immediately, and the landing allows an instant turn --
the 180 that makes it worth doing.

- Needs a cancel state that zeroes upward velocity and applies a fast descent
  rather than waiting out the arc.
- The turn has to be free during the cancel and the landing recovery, or the
  `fTurnRate` cap eats the whole point of it.
- `iLandTicks` is the existing landing recovery; a cancelled landing probably
  wants its own, shorter figure.

## 4. Crouch becomes guard

Rename `bCrouch` / `MECHA_STANCE_CROUCH` / `iBoostCrouchRegen` and everything
that reads them, including the HUD, the briefing control list, `docs/arena-mode.md`
and the tests. Then give a guarding mech real defence against melee: scale
incoming damage and stagger down in `mecha_sim_damage` when the victim is
guarding and the projectile kind is `MECHA_PROJ_MELEE`.

## Open questions -- ask before building

1. Does guard keep the fast boost refill that crouch has today, or is it only
   a defensive posture now?
2. How much should guard cut melee by? Suggest starting at half damage and
   about a third off stagger, then tuning by feel.
3. Should the computer pilot play by the same lock rules? If it does not, it
   is privileged and the difficulty ladder stops meaning anything.

## Warning: this invalidates the skill ladder

The three skill levels in `mecha_ai.c` are calibrated on aim error, measured
over twelve duels, because weapons currently aim themselves perfectly at the
lock. A breakable lock changes what "aiming" even means for the computer
pilot. The figures in the comment above `s_aAiProfiles`, and the ordering the
`ai skill ladder` test asserts, will both have to be re-measured once the lock
work lands. Re-run the sweep rather than assuming the old numbers hold.
