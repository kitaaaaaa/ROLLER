#ifndef _ROLLER_MECHA_TYPES_H
#define _ROLLER_MECHA_TYPES_H
//-------------------------------------------------------------------------------------------------
/*
 * State for ROLLER's arena mode: a 3D mecha duel fought on foot instead of a
 * lap race, built on the same software rasteriser, the same 14-bit heading
 * circle, and the same world scale as the track game.
 *
 * Nothing in this header (or in mecha_sim.c, mecha_ai.c, mecha_arena.c and
 * mecha_defs.c behind it) includes SDL or touches a ROLLER global. The
 * simulation is a pure function of its own state plus one input struct per
 * mech per tick, which is what lets it run headless under the unit tests.
 * The engine-facing half lives in mecha_render.c and mecha_mode.c.
 *
 * The mech roster, arena and artwork are original to this project. What is
 * borrowed from the arcade lineage is the shape of the mechanics -- twin
 * sticks, a boost gauge, weapons that behave differently depending on how
 * you are moving -- not anyone's characters or data.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_math.h"
//-------------------------------------------------------------------------------------------------

#define MECHA_TICK_HZ          60
#define MECHA_TICK_SECONDS     (1.0f / (float)MECHA_TICK_HZ)

#define MECHA_MAX_MECHS         8
#define MECHA_MAX_PROJECTILES 192
#define MECHA_MAX_EFFECTS      96
#define MECHA_MAX_OBSTACLES    24

/*
 * Terrain is a grid of square cells with a height at every corner and a
 * surface word per cell. Coarse on purpose: the hills are meant to be
 * angular, the ground is flat-shaded, and a mech is twelve metres tall, so
 * anything finer would be detail nobody can stand on.
 */
#define MECHA_TERRAIN_CELLS 12
#define MECHA_TERRAIN_NODES (MECHA_TERRAIN_CELLS + 1)

/*
 * Surface bits, and they are the engine's own values -- SURFACE_FLAG_PIT,
 * SURFACE_FLAG_SKIP_RENDER and SURFACE_FLAG_NON_MAGNETIC out of types.h,
 * which this file cannot include because nothing in the simulation may
 * reach into the engine. mecha_render.c includes both and asserts at
 * compile time that they still agree.
 *
 * A pit is a surface, not a hole: the ground is still there and still
 * answers a height query, it is simply flagged as a pit and not drawn.
 * That is how the race game does it, and it is why a machine that walks
 * into one falls in rather than falling through the world.
 */
#define MECHA_SURF_SKIP_RENDER  0x00020000u
#define MECHA_SURF_NON_MAGNETIC 0x00080000u
#define MECHA_SURF_PIT          0x02000000u

/* Left trigger, both triggers, right trigger -- the three shots every mech
 * carries. */
#define MECHA_WEAPON_SLOTS      3
#define MECHA_SLOT_LEFT         0
#define MECHA_SLOT_CENTER       1
#define MECHA_SLOT_RIGHT        2

//-------------------------------------------------------------------------------------------------
/*
 * How the mech is moving when the trigger goes down. Every weapon has a
 * separate definition per stance, so the same trigger is a different attack
 * standing, guarding, dashing or airborne. That one rule is what gives the
 * genre its depth, and it is the reason weapons are a 3 x 4 table rather
 * than a flat list of three.
 */
typedef enum
{
  MECHA_STANCE_STAND  = 0,
  MECHA_STANCE_GUARD = 1,
  MECHA_STANCE_DASH   = 2,
  MECHA_STANCE_JUMP   = 3,
  MECHA_STANCE_COUNT  = 4
} eMechaStance;

//-------------------------------------------------------------------------------------------------

/*
 * What the reticle is actually doing. iTargetIdx says who it is pointed at;
 * this says whether the mech is tracking them. Only MECHA_LOCK_HELD makes
 * the weapons lead their shots and the mech turn itself.
 */
typedef enum
{
  MECHA_LOCK_NONE     = 0,  /* broken: no auto-turn, no lead, no guidance */
  MECHA_LOCK_SLIPPING = 1,  /* outside the cone, inside the grace period */
  MECHA_LOCK_HELD     = 2
} eMechaLockState;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_MOVE_STAND    = 0,
  MECHA_MOVE_WALK     = 1,
  MECHA_MOVE_GUARD    = 2,
  MECHA_MOVE_DASH     = 3,
  MECHA_MOVE_JUMP     = 4,
  /* Guard pressed in the air: the arc is abandoned and the mech drops. It
   * still counts as airborne, and it still poses as a jump, but nothing
   * about it is under the player's control except that it ends sooner. */
  MECHA_MOVE_CANCEL   = 5,
  /* Touchdown recovery. Nothing can be cancelled out of it, which is what
   * makes a jump attack a commitment rather than a free reposition -- the
   * one exception being a cancelled landing, which is shortened and leaves
   * the turn rate off its leash so the mech can come down facing away. */
  MECHA_MOVE_LAND     = 6,
  MECHA_MOVE_STAGGER  = 7,
  MECHA_MOVE_DOWN     = 8,
  MECHA_MOVE_RISE     = 9,
  MECHA_MOVE_DESTROYED = 10
} eMechaMoveState;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_PROJ_BULLET = 0,  /* straight line, no drop */
  MECHA_PROJ_HOMING = 1,  /* steers towards the shooter's lock */
  MECHA_PROJ_BEAM   = 2,  /* fast, flat, pierces nothing but travels far */
  MECHA_PROJ_ARC    = 3,  /* lobbed, falls under its own gravity */
  MECHA_PROJ_MINE   = 4,  /* drops, arms, then detonates on proximity */
  MECHA_PROJ_MELEE  = 5,  /* short-lived hitbox carried in front of the mech */
  /*
   * What a bomb leaves behind: a standing sphere of fire that hurts anything
   * walking into it and swallows shots crossing it. It does not travel and
   * cannot be shot down; it is the one projectile that is only ever a
   * hazard, never a target.
   */
  MECHA_PROJ_SHELL  = 6
} eMechaProjectileKind;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_FX_MUZZLE    = 0,
  MECHA_FX_IMPACT    = 1,
  MECHA_FX_EXPLOSION = 2,
  MECHA_FX_DUST      = 3,
  MECHA_FX_THRUSTER  = 4,
  MECHA_FX_SPARK     = 5,
  /*
   * A thrown, falling, cooling particle. The race game draws its smoke and
   * flames the same way -- a spray of camera-facing squares carrying their
   * own velocity and a palette index, not a sprite sheet -- and the same
   * trick is what turns a blast here from one expanding quad into something
   * that reads as debris.
   */
  MECHA_FX_EMBER     = 6
} eMechaEffectKind;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_CONTROL_NONE  = 0,
  MECHA_CONTROL_HUMAN = 1,
  MECHA_CONTROL_AI    = 2
} eMechaController;

//-------------------------------------------------------------------------------------------------

/*
 * How good the computer pilot is.
 *
 * The pilot has perfect information -- it is reading the same world struct
 * the simulation ticks -- so the levels are not degrees of knowledge but
 * degrees of human limitation put back in: how long it takes to react to a
 * shot, how sure it has to be that a shot will hit before it spends boost
 * dodging, how straight it shoots, and how readily it pulls the trigger.
 * ACE is the pilot with no limitations at all, which is what the mode
 * shipped with and what turned out to be unplayable as a default.
 */
typedef enum
{
  MECHA_AI_ROOKIE  = 0,
  MECHA_AI_VETERAN = 1,
  MECHA_AI_ACE     = 2,
  MECHA_AI_SKILL_COUNT
} eMechaAiSkill;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  MECHA_PHASE_READY      = 0,  /* round announcement, controls locked */
  MECHA_PHASE_FIGHT      = 1,
  MECHA_PHASE_ROUND_OVER = 2,  /* someone is down, the camera lingers */
  MECHA_PHASE_MATCH_OVER = 3
} eMechaPhase;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  const char *szName;
  uint8_t byKind;           /* eMechaProjectileKind */
  uint8_t byCount;          /* shots per pull; > 1 is a spread */
  uint8_t byPalette;        /* tracer colour, palette index */
  int     iSpreadAngle;     /* angle units between adjacent spread shots */
  float   fSpeed;           /* world units per second */
  float   fDamage;
  float   fRadius;          /* projectile collision radius */
  float   fBlastRadius;     /* > 0 detonates and damages within this radius */
  float   fArcGravity;      /* world units per second squared, MECHA_PROJ_ARC */
  int     iLifeTicks;
  int     iAmmo;            /* shots before the slot has to reload */
  int     iReloadTicks;     /* empty to full */
  int     iRecoveryTicks;   /* the shooter cannot act for this long */
  int     iHomingRate;      /* angle units per tick; 0 leaves it flying straight */
  float   fStagger;         /* stagger inflicted; see MECHA_STAGGER_DOWN */
  float   fMuzzleHeight;    /* fraction of mech height the shot leaves from */
  float   fMuzzleSide;      /* lateral muzzle offset, fraction of mech radius */
} tMechaWeaponDef;

//-------------------------------------------------------------------------------------------------
/*
 * A mech's fixed characteristics. The roster in mecha_defs.c is built from
 * these and never changes at runtime, so the whole table is const.
 */
typedef struct
{
  const char *szName;
  const char *szClass;

  /*
   * Silhouette multipliers.
   *
   * Everything the mesh builds is scaled off fHeight and fRadius, which made
   * every machine on the roster the same shape at a different size -- the
   * archetypes existed only in the stat block. These let a siege platform
   * read as one from across the arena: heavy shoulders, thick limbs, an
   * oversized gun in each hand and a head sunk into the chest, against an
   * interceptor that is all narrow torso and thin legs. Zero means one, so a
   * machine that never sets them still builds.
   */
  /*
   * How the machine carries its own weight.
   *
   * The race game's cars do not set their velocity, they drive it: a grip
   * figure limits how fast sideways motion can be corrected, whatever is
   * left over decays on its own, and steering authority falls off as speed
   * rises. The same three ideas are what make a machine here feel like it
   * has mass rather than like a cursor.
   *
   * fGrip is how much sideways velocity is killed per second -- high is
   * crisp, low slides wide out of a turn. fDriveAccel is how hard it can
   * push itself towards the speed it is asking for. fBrake is how quickly
   * it sheds speed with nothing asked of it.
   *
   * All three are absolute, in metres per second squared, and deliberately
   * not multiples of the machine's own walk speed. Scaling them that way
   * normalises out the very thing they exist to express: it makes every
   * machine take the same time to gather itself, so the interceptor -- being
   * simply faster -- slides the furthest, and the siege platform comes out
   * the nimbler of the two.
   */
  float fGrip;
  float fDriveAccel;
  float fBrake;

  float fBuildShoulder;
  float fBuildTorso;
  float fBuildLimb;
  float fBuildHead;
  float fBuildGun;

  float fHeight;            /* world units, ground to head */
  float fRadius;            /* collision cylinder */
  float fMass;              /* scales knockback taken */

  float fArmour;            /* starting and maximum hit points */

  float fWalkSpeed;         /* world units per second */
  float fDashSpeed;
  float fAirSpeed;
  float fTurnRate;          /* angle units per second while free */
  float fJumpVelocity;      /* world units per second at takeoff */

  int   iBoostMax;
  int   iBoostDashDrain;    /* per second while dashing */
  int   iBoostJumpCost;     /* one-off, charged at takeoff */
  int   iBoostJumpDrain;    /* per second while thrusting upward */
  int   iBoostRegen;        /* per second standing or walking */
  int   iBoostGuardRegen;  /* per second guarding -- the fast refill */

  int   iDashTicks;         /* how long one dash burst lasts */
  int   iLandTicks;         /* touchdown recovery */

  /* Palette indices the mesh builder paints with: body, trim, joints, glow. */
  uint8_t abyPalette[4];

  tMechaWeaponDef aWeapons[MECHA_WEAPON_SLOTS][MECHA_STANCE_COUNT];
} tMechaMechDef;

//-------------------------------------------------------------------------------------------------
/*
 * One tick of intent for one mech. The human's pad and the AI both produce
 * this and nothing else, so an AI mech and a player mech are literally the
 * same code path downstream.
 */
typedef struct
{
  int  iMoveX;        /* -100..100, strafe: positive is the mech's right */
  int  iMoveZ;        /* -100..100, positive is forward */
  int  iTurn;         /* -100..100, explicit turn on top of the lock */
  bool bDash;
  bool bJump;
  bool bGuard;
  bool bFireLeft;
  bool bFireCenter;
  bool bFireRight;
  bool bCycleTarget;
} tMechaInput;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  bool    bActive;
  uint8_t byDefIdx;
  uint8_t byController;     /* eMechaController */
  uint8_t byTeam;

  /* Feet position. fY is height above the arena floor, so a grounded mech
   * sits at exactly zero and airborne tests are just fY > 0. */
  float fX, fY, fZ;
  float fVelX, fVelY, fVelZ;

  int   iFacing;            /* 14-bit heading, 0 along +Z */
  int   iAimPitch;          /* 14-bit, clamped to +/- MECHA_AIM_PITCH_LIMIT */

  uint8_t byMove;           /* eMechaMoveState */
  int   iStateTicks;        /* ticks spent in byMove */

  /* Boost is stored pre-multiplied by MECHA_BOOST_SCALE, which is exactly
   * the tick rate. That makes every per-second drain and regen figure in the
   * mech definitions land as an exact per-tick integer delta, so the gauge
   * stays deterministic without carrying a fractional remainder around.
   * mecha_mech_boost_fraction() is what the HUD should read. */
  int   iBoost;
  /* Set when the gauge bottoms out. Dash and jump stay refused until the
   * gauge climbs back past MECHA_BOOST_UNLOCK, so running the tank dry
   * costs a real window rather than one frame. */
  bool  bBoostLocked;

  /* A dash keeps the heading it launched with even while the mech turns to
   * track its target, which is what makes a circle-strafing dash read the
   * way it should. */
  float fDashDirX, fDashDirZ;

  float fArmour;
  float fStagger;           /* bleeds off; crossing MECHA_STAGGER_DOWN floors the mech */
  int   iStunTicks;
  int   iInvulnTicks;

  int   aiAmmo[MECHA_WEAPON_SLOTS];
  int   aiReload[MECHA_WEAPON_SLOTS];
  int   iRecovery;          /* ticks of firing recovery left */
  int   iLastFiredSlot;     /* -1 when nothing has been fired yet */
  int   iLastFiredStance;

  /* A melee swing drags the mech along with it -- the lunge is the attack.
   * Set when a MECHA_PROJ_MELEE weapon fires, and it overrides ordinary
   * movement until it runs out. */
  int   iLungeTicks;
  float fLungeSpeed;

  /* Edge detection. Every trigger in the mode fires on the press rather than
   * on the hold, and the AI produces the same held-button struct a pad does,
   * so the previous frame's state has to live with the mech. */
  bool  abFireHeld[MECHA_WEAPON_SLOTS];
  bool  bJumpHeld;
  bool  bDashHeld;
  bool  bGuardHeld;
  bool  bCycleHeld;

  int   iTargetIdx;         /* who the reticle is on; -1 for nobody */
  uint8_t byLock;           /* eMechaLockState: whether it is tracking them */
  int   iLockSlipTicks;     /* ticks the target has been outside the cone */

  /* Set by a jump cancel's landing. While it runs, the manual turn is
   * uncapped and works even though the landing itself locks out control --
   * that window is the entire reason to cancel. */
  int   iFreeTurnTicks;

  /* While this runs the machine squares itself up on its lock whatever the
   * range. Set by the moves that are supposed to put the enemy back in
   * front of you -- a jump cancel, and firing while boosting or airborne. */
  int   iRecentreTicks;

  /* Angular error added to the firing solution, in the shared 14-bit
   * circle. Weapons aim themselves at whatever is locked, so this is the
   * only thing separating a pilot who can shoot from one who cannot; the
   * computer pilot rolls it per shot and the player leaves it at zero. */
  int   iAimError;

  int   iRoundsWon;
  float fDamageDealt;

  /* Rendering-only smoothing; the simulation never reads these back. */
  float fLeanRoll;
  float fStepPhase;
  /*
   * How much of a fight the machine thinks it is in, 0 to 1. Held while it
   * has a lock or is shooting, and let go of otherwise. Everything above the
   * hips reads it: the stance settles and blades, and the guns come up.
   * Nothing in the simulation reads it back, so a machine animating its way
   * out of a fighting stance is never a machine that has stopped fighting.
   */
  float fCombat;
  /*
   * Where the feet are pointed, which is not where the machine is pointed.
   * The torso holds the aim while the legs follow the line of travel, so a
   * mech strafing across your guns is walking sideways rather than sliding
   * with its shoulders square -- the one piece of animation state the sim
   * has to own, because it is smoothed over time and the mesh is built
   * fresh every frame.
   */
  int   iLegYaw;
  bool  bLegsBackward;      /* stepping backwards: the cycle runs in reverse */
  /*
   * What is left of a boost after the burst itself. The machine keeps the
   * speed it had and steers badly for the length of it, which is what makes
   * a dash a thing you commit to rather than a thing you switch off, and it
   * is the window in which hitting a wall throws you off it.
   */
  int   iCoastTicks;
  /* Where the ground was under it last tick. The difference is how fast the
   * ground is rising, which on a surface that does not hold a machine down
   * is what throws it off the top of a slope. */
  float fGroundY;
  /*
   * Whether the stick has been let go since this dash began. A dash can be
   * steered mid-flight -- release the direction you left on, tap another,
   * and the burst turns -- and the release is what separates that from
   * simply holding a direction down.
   */
  bool  bDashStickFree;
} tMechaMech;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  bool    bActive;
  uint8_t byKind;           /* eMechaProjectileKind */
  uint8_t byOwner;
  uint8_t byPalette;

  float fX, fY, fZ;
  float fPrevX, fPrevY, fPrevZ;   /* start of this tick's segment */
  float fVelX, fVelY, fVelZ;

  float fRadius;
  float fDamage;
  float fBlastRadius;
  float fStagger;
  float fArcGravity;

  int   iLife;
  int   iAge;               /* ticks since launch, for reaction timing */
  int   iHomingRate;
  int   iTarget;            /* -1 for unguided */
  int   iArmTicks;          /* mines ignore everything until this reaches zero */
  /*
   * One bit per mech, for a shell: who has already been burned by it. The
   * blast that spawns it hits everyone standing inside at the time, so those
   * are marked at birth and the shell only catches whoever walks in after.
   */
  uint8_t byHitMask;
} tMechaProjectile;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  bool    bActive;
  uint8_t byKind;           /* eMechaEffectKind */
  uint8_t byPalette;
  float   fX, fY, fZ;
  float   fVelX, fVelY, fVelZ;
  float   fScale;
  int     iAge;
  int     iLife;
} tMechaEffect;

//-------------------------------------------------------------------------------------------------
/*
 * A box standing on the arena floor. Mechs slide along its sides, shots stop
 * against it, and the AI uses it for cover, so one shape covers every
 * obstacle the arena needs.
 */
/* What a piece of cover is made of. All three collide as the same box; the
 * difference is what gets drawn around it. */
typedef enum
{
  MECHA_PROP_BLOCK = 0,
  MECHA_PROP_TREE  = 1,
  MECHA_PROP_ROCK  = 2
} eMechaPropKind;

typedef struct
{
  float fX, fZ;             /* centre on the ground plane */
  float fHalfX, fHalfZ;
  float fHeight;
  uint8_t byKind;           /* eMechaPropKind */
  uint8_t byPalette;
  uint8_t byTrimPalette;
  /* Tiles in the game's building bank, used when the retail data is there.
   * The palette entries above stay the fallback and the shading. */
  uint8_t byTile;
  uint8_t byTopTile;
} tMechaObstacle;

//-------------------------------------------------------------------------------------------------

/*
 * What the boundary is. A square arena is walled on four sides, an octagon
 * on eight, and an open one is not walled at all -- its floor simply stops,
 * and so does anything that walks off it.
 */
typedef enum
{
  MECHA_ARENA_SQUARE  = 0,
  MECHA_ARENA_OCTAGON = 1,
  MECHA_ARENA_OPEN    = 2
} eMechaArenaShape;

typedef struct
{
  const char *szName;
  uint8_t byShape;          /* eMechaArenaShape */
  float fHalfExtent;        /* wall to wall is twice this, or floor to floor */
  float fWallHeight;
  uint8_t byFloorPalette;
  uint8_t byGridPalette;
  uint8_t byWallPalette;
  /* Tiles in the game's track bank, which is where its ground, grass and
   * wall artwork lives. Zero means this surface stays flat-shaded. */
  uint8_t byFloorTile;
  uint8_t byGridTile;
  uint8_t byWallTile;
  int   iObstacleCount;
  tMechaObstacle aObstacles[MECHA_MAX_OBSTACLES];

  /*
   * The ground itself. afNode holds a height per grid corner and auiSurface
   * a surface word per cell; a level arena leaves both at zero and behaves
   * exactly as it did before either existed.
   */
  float    afNode[MECHA_TERRAIN_NODES][MECHA_TERRAIN_NODES];
  uint32_t auiSurface[MECHA_TERRAIN_CELLS][MECHA_TERRAIN_CELLS];
  /* Below this a machine is gone, however it got there. */
  float    fKillY;
} tMechaArena;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  uint8_t byPhase;          /* eMechaPhase */
  int  iPhaseTicks;
  int  iRound;              /* 1-based */
  int  iRoundsToWin;
  int  iRoundTicks;         /* counts down; zero is time up */
  int  iRoundTimeLimit;
  int  iWinnerIdx;          /* -1 for a draw or an unfinished round */
} tMechaMatch;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tMechaArena       arena;
  tMechaMatch       match;
  tMechaMech        aMechs[MECHA_MAX_MECHS];
  tMechaProjectile  aProjectiles[MECHA_MAX_PROJECTILES];
  tMechaEffect      aEffects[MECHA_MAX_EFFECTS];
  tMechaRng         rng;
  uint32_t          uiSeed;
  int               iTick;      /* ticks since the match started */
  int               iMechCount;
  uint8_t           byAiSkill;  /* eMechaAiSkill, applies to every AI mech */
} tMechaWorld;

//-------------------------------------------------------------------------------------------------
#endif
