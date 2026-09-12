#ifndef _ROLLER_MECHA_TYPES_H
#define _ROLLER_MECHA_TYPES_H
//-------------------------------------------------------------------------------------------------
/*
 * State for ROLLER's arena mode: a mecha duel on the track game's own
 * rasteriser, 14-bit heading circle and world scale.
 *
 * Nothing here, or in mecha_sim.c, mecha_ai.c, mecha_arena.c and
 * mecha_defs.c behind it, includes SDL or touches a ROLLER global: the
 * simulation is a pure function of its own state plus one input struct per
 * machine per tick, which is what lets it run headless under the tests. The
 * engine-facing half is mecha_render.c and mecha_mode.c.
 *
 * Roster, arenas and artwork are original to this project. What is borrowed
 * from the arcade lineage is the shape of the mechanics, not anyone's data.
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

/* Terrain: a grid of square cells, a height per corner and a surface word
 * per cell. Coarse on purpose. [ARENA-04] */
/* The array size, not any arena's own division -- each carries its own
 * count in iTerrainCells, and a bigger arena needs more. */
#define MECHA_TERRAIN_CELLS 24
#define MECHA_TERRAIN_NODES (MECHA_TERRAIN_CELLS + 1)
/* What an arena gets when it does not ask for anything else. */
#define MECHA_TERRAIN_CELLS_DEFAULT 12

/*
 * Surface bits, duplicated from the engine's own types.h because nothing in
 * the simulation may include it; mecha_render.c asserts they agree.
 * [TYPE-01]
 *
 * A pit is a surface and not a hole, exactly as in the race game. [SIM-14]
 */
#define MECHA_SURF_SKIP_RENDER  0x00020000u
#define MECHA_SURF_NON_MAGNETIC 0x00080000u
#define MECHA_SURF_PIT          0x02000000u
/* Tarmac rather than whatever the arena's ground normally is. Purely a
 * drawing instruction -- a street holds a wheel exactly as the grass
 * beside it does -- so it lives with the other surface bits rather than
 * needing a second grid. */
#define MECHA_SURF_ROAD         0x04000000u

/* Left trigger, both triggers, right trigger -- the three shots every mech
 * carries. */
#define MECHA_WEAPON_SLOTS      3
#define MECHA_SLOT_LEFT         0
#define MECHA_SLOT_CENTER       1
#define MECHA_SLOT_RIGHT        2

//-------------------------------------------------------------------------------------------------
/*
 * How the machine is moving when the trigger goes down. Every weapon is
 * defined per stance, which is why weapons are a 3 x 4 table rather than a
 * flat list of three.
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
  /* What a bomb leaves behind: a standing fire that hurts anything walking
   * into it and swallows shots crossing it. Never a target. */
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
  /* A thrown, falling, cooling particle, the way the race game draws smoke
   * and flame: camera-facing squares with their own velocity. [MESH-29] */
  MECHA_FX_EMBER     = 6,
  /*
   * A puff off a damaged machine. Rises and spreads rather than falling
   * and cooling, which is the difference between something thrown off an
   * explosion and something pouring out of a hole.
   */
  MECHA_FX_SMOKE     = 7
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
 * How good the computer pilot is. The levels are degrees of human limitation
 * put back in, not degrees of knowledge: the pilot reads the same world the
 * simulation ticks. ACE has none of them. [AI-02]
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
   * Silhouette multipliers, so an archetype reads from across the arena
   * rather than living only in the stat block. Zero means one. [TYPE-02]
   */
  /*
   * How the machine carries its own weight: grip kills sideways velocity,
   * drive pushes towards the speed asked for, brake sheds it with nothing
   * asked. Absolute, in m/s^2, deliberately not multiples of walk speed.
   * [TYPE-03]
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

  /*
   * Wheels instead of legs: no strafe, no boost, no jump, one signed speed
   * along the nose. Everything else -- lock, slots, armour, stagger -- works
   * as it does for the rest of the roster. fSteerFloor is the race game's
   * own rule. [SIM-06]
   */
  bool  bWheeled;
  float fSteerFloor;
  /* What running into somebody costs them, per metre a second over the
   * speed it takes to be worth anything. A machine with no close-quarters
   * weapon still has to have an answer at close quarters. */
  float fRamDamage;
  float fRamSpeed;
  /* How hard firing shoves the machine backwards. A gun the size of the
   * car it is bolted to does not go off quietly. */
  float fRecoilPush;

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

//-------------------------------------------------------------------------------------------------
/*
 * The drawn attitude of one machine, in the pieces the race game keeps it in.
 */
typedef struct
{
  /* The tilt that answers the stick: a car leans out of the corner, a robot
   * into it. [TYPE-04] */
  int   iRollSteer;
  /*
   * Squat and dive. Whiplash's iPitchDynamicOffset: the nose comes up on
   * the throttle at iPitchAccelRate, goes down on the brakes at
   * iPitchDecayRate, and unwinds to level at the recovery rates.
   */
  int   iPitchDrive;
  /* Airborne, the nose follows the velocity vector, as Whiplash's nPitch
   * does. [TYPE-04] */
  int   iAirPitch;
  /* Pitch and roll of the slope actually stood on, sampled across the
   * footprint and eased rather than snapped. [SIM-10] */
  int   iContourPitch;
  int   iContourRoll;
  /* What is left of the last landing: a damped cosine about both axes,
   * seeded from the attitude held at contact. [TYPE-04] */
  float fWobblePitchAmp;
  float fWobbleRollAmp;
  int   iWobblePhase;
  int   iPitchWobble;
  int   iRollWobble;
  /* The body shake: white noise on all three axes, scaled by how hard the
   * machine is working. [TYPE-04] */
  int   iPitchShake;
  int   iRollShake;
  int   iYawShake;
  /*
   * What a legged machine shakes from, since it has no road speed to
   * shake from. Set by taking a hit and bled off, so the shudder belongs
   * to the blow rather than to the walking.
   */
  float fHitShake;
  /*
   * The shake has its own noise so that nothing cosmetic ever reaches into
   * the draw sequence the fight is decided from. A machine rattling on
   * screen must not be able to move an AI pilot's aim by a hair.
   */
  tMechaRng shake;
} tMechaAttitude;

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
  /* Stops a car resting against somebody billing them every tick. */
  int   iRamCooldown;
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

  /* The tick the machine went down on: the grace is the tick rather than
   * the hit, so a whole volley still counts. [SIM-04] */
  int   iDownTick;

  /* Noise for the damage particles. Private, because nothing cosmetic may
   * reach into the sequence a fight is decided from. [SIM-02] */
  tMechaRng spray;

  int   iRoundsWon;
  float fDamageDealt;

  /*
   * How the body sits, as against where the machine is: independent pieces
   * summed at the last moment, the way car.c composes a render pose. All
   * cosmetic, all in the 14-bit circle, never read back. [TYPE-04]
   */
  tMechaAttitude attitude;

  /* Rendering-only smoothing; the simulation never reads these back. */
  float fLeanRoll;
  float fStepPhase;
  /* How much of a fight the machine thinks it is in. Everything above the
   * hips reads it; the simulation never does. [TYPE-05] */
  float fCombat;
  /* Where the feet point, which is not where the machine points. The one
   * piece of animation state the sim owns. [TYPE-05] */
  int   iLegYaw;
  bool  bLegsBackward;      /* stepping backwards: the cycle runs in reverse */
  /* What is left of a boost after the burst: the speed carries and the
   * steering is feeble. [SIM-08] */
  int   iCoastTicks;
  /* Where the ground was under it last tick. The difference is how fast the
   * ground is rising, which on a surface that does not hold a machine down
   * is what throws it off the top of a slope. */
  float fGroundY;
  /* Whether the stick has been let go since this dash began: the release is
   * what makes the crossing step a choice. [SIM-08] */
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

  /*
   * A raised hexagonal mesa, answered by the height query rather than
   * written into the grid so its edges stay hexagonal. Zero height is none.
   * Both radii are apothems. [ARENA-10]
   */
  float    fMesaTop;
  float    fMesaBase;
  float    fMesaHeight;

  /*
   * How far below itself an open arena's edge is drawn. Six metres reads as
   * a platform; two hundred reads as the top of a tower.
   */
  float    fSkirt;

  /* Ground drawn past the boundary, and scenery for it. Not walkable; zero
   * reach draws none of it. [ARENA-07] */
  float    fOuterReach;
  int      iBillboards;

  /* How finely the ground is drawn, which is not how finely it is shaped:
   * zero takes the default. A bigger arena wants more of them or its tiles
   * come out stretched. */
  int      iFloorTiles;
  /* And how finely it is shaped, which is the grid above. Zero takes
   * MECHA_TERRAIN_CELLS_DEFAULT; nothing may exceed MECHA_TERRAIN_CELLS. */
  int      iTerrainCells;

  /*
   * How well the ground holds a wheel, as one of the race game's fourteen
   * grades. Zero is the best, so an arena that says nothing gets the best of
   * it and only a slippery one has to say so. [ARENA-13]
   */
  uint8_t  byGripLevel;
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
  /*
   * A debug switch, not a difficulty: the computer pilots go on fighting for
   * position exactly as they would, they simply never pull a trigger. It is
   * there so the movement can be looked at without being shot while looking.
   */
  bool              bAiHoldFire;
} tMechaWorld;

//-------------------------------------------------------------------------------------------------
#endif
