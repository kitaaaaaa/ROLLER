#ifndef _ROLLER_MECHA_DEFS_H
#define _ROLLER_MECHA_DEFS_H
//-------------------------------------------------------------------------------------------------
/*
 * The machine roster and the balance constants the simulation reads. Every
 * machine is original to ROLLER, spread along one axis: how close you have to
 * get to do damage.
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
 * The lock is breakable, and getting it back is harder than keeping it: a
 * generous cone to hold, a narrow one to reacquire, and a boost or a jump to
 * snap it on from any angle. [DEF-01]
 */
#define MECHA_LOCK_CONE          MECHA_DEG(32)
#define MECHA_LOCK_REACQUIRE_CONE MECHA_DEG(12)
#define MECHA_LOCK_BREAK_TICKS   MECHA_SEC(0.35f)

/*
 * Guard: the fastest refill, its own row of weapons, and 15% of a melee hit.
 * Stagger is cut by half rather than by the same 85%, so a guarded blade
 * still rocks what it lands on. [DEF-02]
 */
#define MECHA_GUARD_MELEE_DAMAGE  0.15f
#define MECHA_GUARD_MELEE_STAGGER 0.50f

/* The jump cancel: up to find them, down to face them. [DEF-03] */
/* A cancelled jump does not fall, it is dropped. [DEF-03] */
#define MECHA_CANCEL_FALL_SPEED  MECHA_MPS(120.0f)
#define MECHA_CANCEL_LAND_TICKS  MECHA_SEC(0.12f)
#define MECHA_CANCEL_TURN_TICKS  MECHA_SEC(0.45f)
#define MECHA_CANCEL_TURN_SCALE  7

/*
 * Tiles in the game's own texture banks, chosen by measuring the decoded
 * banks for uniformity rather than by eye, and paired so the two alternating
 * across a floor read as one surface. [DEF-04]
 *
 * Reached only when the retail data is installed; each surface keeps a
 * palette index for the flat fallback.
 */
/* Tarmac to draw a street with, in both the palette a data-less checkout
 * falls back to and the retail tiles. The two alternate so a road keeps
 * the same checker the ground beside it has. */
#define MECHA_SHADE_ROAD_A         123
#define MECHA_SHADE_ROAD_B         126
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
 * Inside this the machine keeps its shoulders square to whatever it has
 * locked; outside it, pointing the thing is the player's job. [DEF-05]
 */
#define MECHA_CLOSE_QUARTERS   MECHA_M(34.0f)
/*
 * How much further out the chase camera lets go of a car and frames the
 * enemy instead: a car is small, fast and rarely pointing where it is going.
 * [REND-03]
 */
#define MECHA_CAM_CAR_RANGE    1.5f

/* How long a move that re-centres holds the machine on its lock: a move,
 * not a mode. [DEF-05] */
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
 * A boost is a committed act: the clock, an empty gauge, a jump or a wall
 * ends it, and letting go does not. What follows is the coast. [SIM-08]
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
 * How fast the ground must rise under a machine before a non-magnetic
 * surface throws it off the top: above a walk, well below a boost.
 * [SIM-13]
 */
/*
 * How far ahead a computer pilot checks there is still an arena under it.
 * Three questions, so three distances: a stride on foot, the whole burst
 * before pressing boost, and only as far as a cancel needs once it is under
 * way. [AI-05]
 */
/* How far ahead a pilot looks for something to climb onto, and the smallest
 * step worth jumping for. [AI-08] */
#define MECHA_AI_CLIMB_LOOK  MECHA_M(7.0f)
#define MECHA_AI_CLIMB_MIN   MECHA_M(4.0f)

#define MECHA_AI_FOOTING_WALK   MECHA_M(16.0f)
#define MECHA_AI_FOOTING_LEAD   0.35f
#define MECHA_AI_FOOTING_CANCEL 0.60f
#define MECHA_AI_FOOTING_AIR    0.80f

#define MECHA_RAMP_LAUNCH_CLIMB MECHA_MPS(18.0f)

/*
 * Rolling down is not falling down: the steepest slope a machine already in
 * contact will follow rather than fall away from in steps. A gradient, not a
 * rate, so a hillside holds at any speed and a roof lip is a cliff at any
 * speed. [SIM-12]
 */
#define MECHA_RAMP_STICK_GRADE  1.0f
/* Below this there is no meaningful forward speed to measure a gradient
 * against, and a machine going nowhere is not driving off anything. */
#define MECHA_RAMP_STICK_SPEED  MECHA_MPS(3.0f)

/* Metres of ground per stride. Doubled to halve the cycle rate: the legs
 * were turning over twice as fast as the machines read as moving. */
#define MECHA_STRIDE_METRES     MECHA_M(10.4f)

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

/* Two ways to change a committed dash: the cancel and the crossing step.
 * [SIM-08] */
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
 * Steering, the way the race game does it: hardest just above a walking
 * pace, gone below the steering floor, and with no ceiling on the
 * accumulated yaw. A gain of six on the slack is Whiplash's own curve.
 * [SIM-06]
 */
#define MECHA_CAR_STEER_GAIN 6.0f
/* How much of its forward speed it will do backwards, how hard it slows
 * with nothing pressed, and above what speed the wheels are rolling rather
 * than the car standing still. */
#define MECHA_CAR_REVERSE    0.34f
#define MECHA_CAR_DRAG       0.22f
#define MECHA_CAR_ROLLING    MECHA_MPS(2.0f)
/*
 * Running somebody over: how square the hit must be to count, how long
 * before the same car can do it again, and how much stagger comes with it.
 * [SIM-17]
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
 * Wearing the damage, the way the race game shows it.
 *
 * Whiplash computes a health factor of (health + 34) / 100, clamped to
 * one, and its dospray() starts throwing particles once that drops below
 * 0.66 -- widening from two emitter points to four below 0.75, and with a
 * per-frame chance of `rand() * factor < 8192` so the rate climbs as the
 * car gets worse. The 0.66 and the shape of that probability are the race
 * game's; everything below is this project's, because Whiplash has one
 * damage tier and this wants two.
 *
 * Smoke from a machine that is hurt, fire from one that is nearly gone.
 */
#define MECHA_DAMAGE_SMOKE     0.66f
#define MECHA_DAMAGE_FIRE      0.33f
/* Numerator of the race game's own emission chance, against a health
 * factor: at the smoke threshold that is one tick in three, and by the
 * time a machine is burning it is almost every tick. */
#define MECHA_DAMAGE_RATE      0.22f
/*
 * And how often it may even try.
 *
 * The effect table is ninety-six slots shared by everything -- blasts,
 * debris, dust, muzzle flashes. Whiplash can be generous here because
 * every car owns a private thirty-two-slot spray array; this cannot. Left
 * to roll every tick, one machine at fifteen per cent health held
 * sixty-five slots at once and would have starved the explosion that
 * finally killed it. One roll every five ticks, staggered between
 * machines, keeps a burning wreck to about eighteen.
 */
#define MECHA_DAMAGE_INTERVAL  5
#define MECHA_DAMAGE_SMOKE_LIFE MECHA_SEC(1.0f)
#define MECHA_DAMAGE_FIRE_LIFE  MECHA_SEC(0.55f)
/* How far up the machine the damage sits, and how far it drifts. */
#define MECHA_DAMAGE_HEIGHT    0.62f
#define MECHA_DAMAGE_RISE      MECHA_MPS(7.0f)
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
/* And how much faster it comes round while that runs. A spin onto the
 * lock that takes as long as an ordinary lock-follow is not a spin. */
#define MECHA_RECENTRE_SCALE   4

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
/*
 * Rolling off a cambered launch, the way the race game does it. Whiplash
 * sets iRollMomentum to chunk camber * speed / 720 per tick at 36 Hz
 * (control.c), and 360 is its reference speed -- so at full speed that is
 * half the camber a tick, and the same rotation per second at 60 Hz is
 * three tenths of it against the machine's own top speed.
 * A landing more than a quarter turn from level is a landing on the roof.
 * [SIM-18]
 */
/* Damage per unit of speed a car carries into something while airborne.
 * Whiplash charges the reflected approach speed times 0.005. [SIM-19] */
#define MECHA_AIR_BOUNCE_DAMAGE 0.005f

/* The golden angle in radians, which is what spaces a packed cone of shot
 * evenly instead of in arms. [SIM-20] */
#define MECHA_SPREAD_GOLDEN 2.39996323f

#define MECHA_CAMBER_SPIN_GAIN 0.30f
#define MECHA_CAMBER_UPRIGHT   MECHA_ANGLE_QUARTER

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
