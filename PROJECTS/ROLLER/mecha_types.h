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
  MECHA_PROJ_MELEE  = 5   /* short-lived hitbox carried in front of the mech */
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
typedef struct
{
  float fX, fZ;             /* centre on the ground plane */
  float fHalfX, fHalfZ;
  float fHeight;
  uint8_t byPalette;
  uint8_t byTrimPalette;
  /* Tiles in the game's building bank, used when the retail data is there.
   * The palette entries above stay the fallback and the shading. */
  uint8_t byTile;
  uint8_t byTopTile;
} tMechaObstacle;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  const char *szName;
  float fHalfExtent;        /* the arena is square, wall to wall is twice this */
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
