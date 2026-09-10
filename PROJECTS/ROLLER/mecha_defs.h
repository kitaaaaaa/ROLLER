#ifndef _ROLLER_MECHA_DEFS_H
#define _ROLLER_MECHA_DEFS_H
//-------------------------------------------------------------------------------------------------
/*
 * The mech roster and the balance constants the simulation reads.
 *
 * Every machine here is original to ROLLER. The four are deliberately spread
 * across one axis -- how close you have to get to do damage -- so the same
 * three triggers play differently depending on what you picked.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

/* Metres, metres per second, and degrees, in the units the simulation
 * actually stores. Writing the tables in real-world figures is the only way
 * the balance stays readable. */
#define MECHA_M(x)    ((float)(x) * MECHA_METRE)
#define MECHA_MPS(x)  ((float)(x) * MECHA_METRE)
#define MECHA_DEG(x)  ((int)((float)(x) * (float)MECHA_ANGLE_FULL / 360.0f))
#define MECHA_SEC(x)  ((int)((float)(x) * (float)MECHA_TICK_HZ))

//-------------------------------------------------------------------------------------------------
/* Balance constants shared by every mech. */

#define MECHA_GRAVITY          MECHA_MPS(34.0f)  /* per second, per second */

/* Stagger accumulates from hits and bleeds off; crossing the threshold puts
 * the mech on the floor. Chip damage alone never floors anyone -- it takes a
 * heavy hit or a fast enough chain of light ones. */
#define MECHA_STAGGER_DOWN     100.0f
#define MECHA_STAGGER_DECAY     55.0f            /* per second */

#define MECHA_DOWN_TICKS       MECHA_SEC(1.1f)
#define MECHA_RISE_TICKS       MECHA_SEC(0.45f)
/* Invulnerability covers getting up, so a floored mech is never chained. */
#define MECHA_RISE_INVULN      MECHA_SEC(0.8f)
#define MECHA_STAGGER_TICKS    MECHA_SEC(0.35f)

/* The gauge has to climb back to this fraction of full before dash and jump
 * unlock again after bottoming out. */
#define MECHA_BOOST_UNLOCK_NUM 30
#define MECHA_BOOST_UNLOCK_DEN 100

/* How far off the mech's own heading it may aim vertically. */
#define MECHA_AIM_PITCH_LIMIT  MECHA_DEG(38)

/* Beyond this the lock breaks; the reticle goes cold and homing stops. */
#define MECHA_LOCK_RANGE       MECHA_M(260.0f)

/*
 * The lock is breakable.
 *
 * Weapons aim themselves at whatever is locked, so a lock that can never be
 * lost means the fight is decided entirely by the feet. Holding it is a
 * skill instead: it survives while the target is inside a generous cone of
 * the mech's own heading, and once it has been outside for the grace period
 * it drops -- the auto-turn stops following, shots fire straight down the
 * barrel with no lead, and missiles launch unguided.
 *
 * Getting it back is deliberately harder than keeping it. On its own it
 * returns only when the target is well inside the much narrower reacquire
 * cone. The quick way back is to boost or jump, either of which snaps it on
 * from any angle -- which is what makes those two worth spending gauge on
 * beyond the distance they cover.
 */
#define MECHA_LOCK_CONE          MECHA_DEG(32)
#define MECHA_LOCK_REACQUIRE_CONE MECHA_DEG(12)
#define MECHA_LOCK_BREAK_TICKS   MECHA_SEC(0.35f)

/*
 * Guard.
 *
 * It was a crouch, and it still refills the gauge fastest and still selects
 * its own row of weapons. What it adds is a hard answer to being closed on:
 * a guarding mech takes 15% of a melee hit. Only melee -- guard is a stance,
 * not a shield, and standing in it against gunfire has to lose, or the fast
 * refill it already grants would make it the only thing anyone does.
 *
 * Stagger is cut by half rather than by the same 85%, so a guarded blade
 * still rocks the machine it lands on. Reading the swing should win the
 * exchange outright; it should not make the swing feel like nothing
 * happened, and leaving some stagger on is what keeps a blade rush worth
 * committing to even against someone who saw it coming.
 */
#define MECHA_GUARD_MELEE_DAMAGE  0.15f
#define MECHA_GUARD_MELEE_STAGGER 0.50f

/*
 * Jump cancel.
 *
 * A jump snaps the lock on, which makes going up the reliable way to find an
 * opponent who has got behind you. Guard in the air then drops the mech
 * straight down instead of riding the arc out, and the landing leaves the
 * turn rate off its leash for a moment -- long enough to come down facing
 * the other way. That pair is the whole move: up to find them, down to face
 * them.
 */
#define MECHA_CANCEL_FALL_SPEED  MECHA_MPS(46.0f)
#define MECHA_CANCEL_LAND_TICKS  MECHA_SEC(0.12f)
#define MECHA_CANCEL_TURN_TICKS  MECHA_SEC(0.45f)
#define MECHA_CANCEL_TURN_SCALE  7

/*
 * Tiles in the game's own texture banks.
 *
 * Chosen by measuring the decoded banks, not by eye. track1.drh holds 246
 * tiles and most of them are track furniture -- kerbs, arrows, lane
 * markings, a sponsor emblem -- none of which survives being tiled across a
 * floor. What a ground surface needs is uniformity, so the candidates were
 * ranked by the standard deviation of their luminance and the flattest ones
 * taken: 54 and 55 are the same grey a shade apart, 205 and 210 clean grass,
 * 12 and 13 concrete and rust. A pair has to be neighbours in appearance as
 * well, because the two alternate across the floor the way the two palette
 * entries do -- pairing plain tarmac with a lane-marked tile turned the
 * arena into a chessboard instead of a surface.
 *
 * Every one of these is reached only when the retail data is installed;
 * each surface keeps a palette index for the flat fallback.
 */
#define MECHA_TILE_TARMAC_A        54
#define MECHA_TILE_TARMAC_B        55
#define MECHA_TILE_GRASS_A        205
#define MECHA_TILE_GRASS_B        210
#define MECHA_TILE_PLATE_A         12
#define MECHA_TILE_PLATE_B         13
#define MECHA_TILE_CONCRETE        12
#define MECHA_TILE_BRICK            9
#define MECHA_TILE_RUST            14

/* Building facades for the sides of cover, and the flattest panel in that
 * bank for the roofs -- a facade laid on its back reads as a building that
 * has fallen over. */
#define MECHA_TILE_FACADE_FIRST    11
#define MECHA_TILE_FACADE_COUNT     8
#define MECHA_TILE_ROOF             8

/*
 * Inside this the machine keeps its own shoulders square to whatever it has
 * locked; outside it, pointing the thing is the player's job.
 *
 * The auto-turn used to run at every range, which quietly took the steering
 * away: there was no distance at which a player chose where the machine was
 * facing. Confining it to knife range keeps the one place it earns its
 * keep -- a melee exchange is too fast to aim by hand -- and gives the
 * rest of the fight back. The lock is unaffected either way; it still
 * decides whether the weapons lead, and holding it at range now means
 * actually keeping the enemy in front of you.
 */
#define MECHA_CLOSE_QUARTERS   MECHA_M(34.0f)

/*
 * How long a move that re-centres holds the machine on its lock.
 *
 * Long enough to complete the turn and let a shot go, short enough that it
 * is a move rather than a mode -- the auto-turn coming back on permanently
 * would undo the point of confining it to knife range.
 */
/*
 * How far the feet are allowed to point away from the shoulders, and how
 * fast they get there. Past the clamp the machine walks with its legs
 * crossed, which reads as a mistake rather than as a strafe.
 */
/*
 * How far the arms and the head are allowed off the shoulders, and how hard
 * a shot kicks the arm that fired it. The arms reach further round than the
 * head does: an arm is a gun mount, a neck is a neck.
 */
/*
 * A boost is a committed act. The button starts it and nothing but the
 * clock, an empty gauge, a jump or a wall ends it -- letting go does not.
 * What is left afterwards is the coast: the speed carries, the steering is
 * feeble, and the machine skids rather than turning.
 */
/* How far a machine travels per stride of the walk cycle. */
/*
 * The fireball a bomb leaves standing. It opens from a third of the blast
 * radius to all of it, and for as long as it is there it burns anything
 * that walks in and eats anything shot through it.
 */
#define MECHA_SHELL_TICKS       MECHA_SEC(0.42f)
#define MECHA_SHELL_OPEN        0.34f
/* Walking into the fire afterwards is worth less than being caught by the
 * blast itself, which has already been paid out. */
#define MECHA_SHELL_TOUCH       0.5f

/*
 * Shots meeting in the air. Within this much of each other's damage they
 * trade -- both gone -- and outside it the heavier one carries on through.
 */
#define MECHA_SHOT_TRADE_MARGIN 0.15f

#define MECHA_STRIDE_METRES     MECHA_M(3.6f)

#define MECHA_DASH_COAST_TICKS  MECHA_SEC(0.55f)
#define MECHA_COAST_ACCEL_SCALE 0.22f
#define MECHA_COAST_GRIP_SCALE  0.18f

/*
 * Two ways to change a dash you are already committed to. Boost again while
 * pushing back against it and the burst restarts the other way -- that is
 * the cancel. Or let the stick go and tap a new direction, and the burst
 * turns without a second press, which is the crossing step: cheaper, but it
 * costs the moment it takes to release.
 */
#define MECHA_DASH_CANCEL_DOT   (-0.35f)
#define MECHA_DASH_STICK_FREE   0.20f
#define MECHA_DASH_STICK_TAP    0.45f

/*
 * Off a wall at speed. The race game's cars bounce rather than stopping
 * dead, and a machine carrying a boost into a wall should do the same:
 * below this speed it just leans on the wall, above it, it comes off.
 */
#define MECHA_BOUNCE_RESTITUTION 0.55f
#define MECHA_BOUNCE_MIN_SPEED   MECHA_MPS(9.0f)

#define MECHA_ARM_YAW_LIMIT    MECHA_DEG(46)
#define MECHA_ARM_PITCH_LIMIT  MECHA_DEG(38)
#define MECHA_ARM_DROOP        MECHA_DEG(22)
#define MECHA_ARM_RECOIL       MECHA_DEG(14)
#define MECHA_HEAD_YAW_LIMIT   MECHA_DEG(38)
#define MECHA_HEAD_PITCH_LIMIT MECHA_DEG(20)

#define MECHA_LEG_YAW_LIMIT   MECHA_DEG(52)
#define MECHA_LEG_YAW_RATE    MECHA_DEG(320)   /* per second */
/* Below this the legs have no line of travel to follow and square up. */
#define MECHA_LEG_WALK_SPEED  MECHA_MPS(1.2f)

#define MECHA_RECENTRE_TICKS   MECHA_SEC(0.5f)

/* Mechs push each other apart rather than overlapping. */
#define MECHA_PUSH_PER_TICK    MECHA_M(0.9f)

//-------------------------------------------------------------------------------------------------

int mecha_def_count(void);
/* Out-of-range indices wrap, so callers can cycle a select screen freely. */
const tMechaMechDef *mecha_def_get(int iDefIdx);

//-------------------------------------------------------------------------------------------------
#endif
