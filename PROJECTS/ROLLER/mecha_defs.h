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
/*
 * A cancelled jump does not fall, it is dropped. Forty-six metres a second
 * was a brisk fall; at a hundred and twenty the machine is simply on the
 * ground, which is what makes the cancel a way of getting out of an arc
 * rather than a slightly faster way of finishing it.
 */
#define MECHA_CANCEL_FALL_SPEED  MECHA_MPS(120.0f)
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

/*
 * How fast the ground has to be rising under a machine before a surface
 * that does not hold it down throws it off the top. Above a walk and well
 * below a boost, so climbing a hill on foot keeps your feet on it and
 * boosting up the same hill does not.
 */
/*
 * How far ahead a computer pilot checks there is still an arena under it.
 *
 * Three different questions, so three different distances. On foot it is a
 * stride of reaction plus what the machine is carrying. Before pressing
 * boost it is the whole burst, because that is the distance the press buys
 * and there is no taking it back. And once the burst is under way it is
 * only as far as a cancel needs -- looking any further has the pilot
 * flinching at an edge it was always going to stop short of, and a panicked
 * counter-burst is its own way off a roof. In the air it is about how long
 * the machine has left before it comes down, since that is the only part of
 * the arc it still gets a say in.
 */
#define MECHA_AI_FOOTING_WALK   MECHA_M(16.0f)
#define MECHA_AI_FOOTING_LEAD   0.35f
#define MECHA_AI_FOOTING_CANCEL 0.60f
#define MECHA_AI_FOOTING_AIR    0.80f

#define MECHA_RAMP_LAUNCH_CLIMB MECHA_MPS(18.0f)

#define MECHA_STRIDE_METRES     MECHA_M(5.2f)

/*
 * How fast the machine brings its guns up and how slowly it puts them down,
 * in fractions of the way there per second. See tMechaMech::fCombat.
 */
/* Seconds a round runs for unless the briefing says otherwise. */
#define MECHA_ROUND_SECONDS     90

#define MECHA_COMBAT_RAISE      7.0f
#define MECHA_COMBAT_LOWER      1.6f

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

/*
 * Steering, the way the race game does it.
 *
 * Whiplash works the lock out as `input * (1 + (360 - speed) / 60)` and
 * then throws it away entirely below the car's own steering speed limit --
 * so a car turns hardest just above a walking pace, loses lock as it gets
 * quicker, and cannot turn at all standing still. Both halves are what
 * makes driving one feel like driving rather than like walking on wheels,
 * and the second half is why a machine on wheels has to keep moving to
 * point at anything.
 *
 * 360 is that game's reference speed, so the divisor is a sixth of it and
 * the bonus runs from seven times the input at a standstill to nothing at
 * all flat out. Written here against the machine's own top speed, that is
 * a gain of six on the slack, which is the same curve.
 *
 * And there is no ceiling on any of it. The yaw is simply accumulated:
 * nothing in the race game limits how far a car may come round, which is
 * why one can be spun through a whole circle on the stick in a drift. The
 * grip decides whether the car goes where its nose has gone, and that is a
 * separate number.
 */
#define MECHA_CAR_STEER_GAIN 6.0f
/* How much of its forward speed it will do backwards, how hard it slows
 * with nothing pressed, and above what speed the wheels are rolling rather
 * than the car standing still. */
#define MECHA_CAR_REVERSE    0.34f
#define MECHA_CAR_DRAG       0.22f
#define MECHA_CAR_ROLLING    MECHA_MPS(2.0f)
/*
 * Running somebody over. How square the hit has to be to count as driving
 * into them, how long before the same car can do it again -- without which
 * a car resting against somebody bills them sixty times a second -- and how
 * much stagger comes with the damage.
 */
#define MECHA_CAR_RAM_DOT     0.4f
#define MECHA_CAR_RAM_TICKS   MECHA_SEC(0.55f)
#define MECHA_CAR_RAM_STAGGER 0.9f
/*
 * Rounds in the gun car's magazine, shared across all three triggers.
 * Named because the roster and the test both have to agree about it.
 */
#define MECHA_CAR_MAGAZINE    9

/*
 * How the computer pilot drives. Inside the first it stops steering, which
 * is what lets the gun settle; outside the second it lifts off, because the
 * wheels bite hardest below the top speed and a car flat out understeers
 * past everything it is aiming at.
 */
#define MECHA_AI_DRIVE_STRAIGHT MECHA_DEG(4)
#define MECHA_AI_DRIVE_LIFT     MECHA_DEG(52)

#define MECHA_ARM_YAW_LIMIT    MECHA_DEG(46)
#define MECHA_ARM_PITCH_LIMIT  MECHA_DEG(38)
#define MECHA_ARM_DROOP        MECHA_DEG(22)
#define MECHA_ARM_RECOIL       MECHA_DEG(14)

/*
 * How long the close-quarters blade is drawn, against the hitbox it
 * carries. A sword reads as a sword by being much longer than it is wide,
 * and the hitbox is a sphere, so the drawing has to be the longer thing.
 *
 * Sized so the point lands just past the edge of the hitbox rather than
 * well beyond it: a blade drawn longer than its reach teaches the player
 * a range the weapon does not have.
 */
#define MECHA_BLADE_REACH      1.6f
/*
 * At rest the whole arm unfolds and hangs: the shoulder stops tracking, the
 * elbow gives up all but this much of its right angle, and the gun ends up
 * pointed at the floor beside the machine's own foot. It is the clearest
 * read in the game for "this one is not shooting at you".
 */
#define MECHA_ARM_REST_ELBOW   MECHA_DEG(11)
#define MECHA_HEAD_YAW_LIMIT   MECHA_DEG(38)
#define MECHA_HEAD_PITCH_LIMIT MECHA_DEG(20)

#define MECHA_LEG_YAW_LIMIT   MECHA_DEG(52)
#define MECHA_LEG_YAW_RATE    MECHA_DEG(320)   /* per second */
/* Squaring up to a boost is quicker than that: the burst is over in less
 * than a second and legs still catching up look broken. */
#define MECHA_LEG_DASH_RATE   MECHA_DEG(900)
/* Below this the legs have no line of travel to follow and square up. */
#define MECHA_LEG_WALK_SPEED  MECHA_MPS(1.2f)

#define MECHA_RECENTRE_TICKS   MECHA_SEC(0.5f)

/* Mechs push each other apart rather than overlapping. */
#define MECHA_PUSH_PER_TICK    MECHA_M(0.9f)

//-------------------------------------------------------------------------------------------------
/*
 * Attitude: the numbers the race game draws its cars with.
 *
 * All of these are lifted from Whiplash rather than invented. The engine
 * table in engines.c gives every car the same figures for this -- the
 * differences between cars are in the gearing and the grip, not in how
 * the body sits -- so they are written here as the constants they are.
 * Both games count a full circle as 16384, so the angles carry over
 * untouched; only the rates need scaling, because Whiplash's control loop
 * runs at 36 Hz (control.c advances lap time by 1/36 a tick) and this one
 * runs at 60.
 */
#define MECHA_WHIP_HZ          36.0f
#define MECHA_WHIP_RATE(x)     ((int)((float)(x) * MECHA_WHIP_HZ \
                                      / (float)MECHA_TICK_HZ + 0.5f))

/* iRollResponseRate, iMaxRollOffset, iRollCenteringRate. The limit is two
 * and a fifth degrees, which is the whole of the effect. */
#define MECHA_TILT_RATE        MECHA_WHIP_RATE(10)
#define MECHA_TILT_LIMIT       100
#define MECHA_TILT_CENTRE      MECHA_WHIP_RATE(30)

/* iPitchAccelRate, iMaxPitchOffset, iPitchDecayRate, iMinPitchOffset and
 * the two recovery rates, which the table gives as one number twice. */
#define MECHA_SQUAT_RATE       MECHA_WHIP_RATE(4)
#define MECHA_SQUAT_LIMIT      80
#define MECHA_SQUAT_RECOVER    MECHA_WHIP_RATE(8)

/*
 * iOscillationFreq, and the pair fOscillationMax/fOscillationMin that
 * Whiplash blends between by amplitude -- max is the *smaller* number, so
 * a big wobble dies faster than a small one, which is the one thing about
 * this that looks wrong written down and right on screen. Roll decays at a
 * flat 0.9 in the original. Raised to 36/60 so a second of decay is a
 * second of decay at either rate.
 */
#define MECHA_WOBBLE_FREQ      MECHA_WHIP_RATE(1200)
#define MECHA_WOBBLE_DECAY_MAX 0.95f
#define MECHA_WOBBLE_DECAY_MIN 0.97f
#define MECHA_WOBBLE_ROLL_DECAY 0.9f
/* Whiplash's 0.00024414062, which is 1/4096: amplitude measured in
 * quarter-circles is what picks the point between the two decay rates. */
#define MECHA_WOBBLE_BLEND     (1.0f / 4096.0f)
/* Below this there is nothing left to see and the oscillator is stopped,
 * so a parked car is not quietly running a sine wave forever. */
#define MECHA_WOBBLE_FLOOR     4.0f
/*
 * The most a landing can rock a body.
 *
 * Whiplash seeds this straight from the attitude at contact and gets away
 * with it because a car coming off a racetrack jump is barely pitched. A
 * machine dropping off the side of Tower Seven is pitched a great deal
 * further than that, and feeding the whole of it in gives a landing that
 * reads as a crash. Clamping the seed keeps what the original is for --
 * a gentle touchdown barely registers and a hard one rings -- without
 * letting a long fall turn the car over.
 */
#define MECHA_WOBBLE_LIMIT     MECHA_DEG(8)
/* And how hard it has to come down to ring at all. Without this, a car
 * jostling over broken ground touches down with a trace of fall speed
 * every few ticks and re-seeds the oscillator each time, which is a car
 * permanently shivering rather than one that has landed. */
#define MECHA_WOBBLE_MIN_DROP  MECHA_MPS(6.0f)

/*
 * The shake. Whiplash computes (rand - 0x4000) * damage * speed /
 * iStabilityFactor per axis per tick, where damage runs 1 at full health
 * to 8 at wrecked. iStabilityFactor is 262144 for every engine in the
 * table; with speed expressed here as a fraction of the machine's own top
 * speed rather than in the race game's road units, the divisor becomes a
 * gain instead, chosen to land the healthy car at the same fraction of a
 * degree the original does.
 */
#define MECHA_SHAKE_DAMAGE_MAX 8.0f
#define MECHA_SHAKE_GAIN       22.0f
/* A hit shakes a legged machine, since it has no road speed to shake from.
 * Full deflection from one heavy blow, bled off over about a second. */
#define MECHA_SHAKE_HIT_PER_HP 0.016f
#define MECHA_SHAKE_HIT_DECAY  1.1f            /* per second */

/*
 * Which way the tilt goes. Whiplash's cars lean out of the corner; the
 * robots lean into it. The sign is the entire difference and it is worth
 * naming rather than burying in a minus.
 *
 * Positive roll in the pose lifts the machine's right side, so a positive
 * angle leans it *left*. That is not obvious from the matrix and was got
 * wrong the first time; it was settled by building a mesh at a known roll
 * and measuring which flank came out lower, which is the only way to be
 * sure of a sign convention.
 */
#define MECHA_TILT_CAR_SIGN    (1)
#define MECHA_TILT_MECH_SIGN   (-1)
/* The robots get a little more of it than the cars do, because a machine
 * that tall shows less of a given angle. Still under three degrees. */
#define MECHA_TILT_MECH_LIMIT  128
/*
 * How far the nose can follow the velocity vector. Whiplash lets a car
 * point wherever it is falling, but it draws that against a road; here a
 * long drop off Tower Seven would have the car pointing straight down,
 * which reads as a crash rather than as a jump.
 */
#define MECHA_AIR_PITCH_LIMIT  MECHA_DEG(35)

/*
 * Standing on the ground rather than merely above it.
 *
 * How far the machine's footprint reaches when it asks what the ground is
 * doing -- fore and aft against across, because a car is longer than it is
 * wide and a wheelbase reads a slope differently from a track. Both are
 * measured against the machine's own collision radius, so this scales with
 * whatever is driving.
 */
#define MECHA_CONTOUR_WHEELBASE 1.6f
#define MECHA_CONTOUR_TRACK     1.0f
/* Sampling the terrain is only right while the machine is on the terrain;
 * up on the roof of a box it is standing on something flat that the height
 * field underneath knows nothing about. */
#define MECHA_CONTOUR_CONTACT   MECHA_M(0.6f)
/* Suspension, more or less: fast enough to follow ground crossed at speed,
 * slow enough that a gradient changing between grid cells does not snap. */
#define MECHA_CONTOUR_RATE      MECHA_DEG(240)   /* per second */
/* Nothing the arenas contain is this steep, so this only ever catches a
 * sampling artefact at an edge. */
#define MECHA_CONTOUR_LIMIT     MECHA_DEG(38)

//-------------------------------------------------------------------------------------------------

int mecha_def_count(void);
/* Out-of-range indices wrap, so callers can cycle a select screen freely. */
const tMechaMechDef *mecha_def_get(int iDefIdx);

//-------------------------------------------------------------------------------------------------
#endif
