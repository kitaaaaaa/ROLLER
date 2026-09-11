#include "mecha_defs.h"

//-------------------------------------------------------------------------------------------------
/*
 * Tracer and hull palette indices.
 *
 * The tracer entries are the ones ROLLER's own firework table already uses
 * (function.c), so they are known-bright in the stock palette. The hull
 * entries are tuned by eye against the same palette; every colour the mode
 * paints with is named here or in mecha_arena.c so a retune stays local.
 */
/*
 * Measured against the game's own PALETTE.PAL, not chosen to look right in
 * the mode's fallback table. Several of these were picked before the retail
 * palette was ever loaded and were badly wrong in it -- the "green" tracer
 * came out blue, "amber" magenta and "orange" pink -- which nothing noticed
 * while the mode only ever drew through its own colours.
 */
#define PAL_TRACER_ORANGE 171
#define PAL_TRACER_AMBER  206
#define PAL_TRACER_VIOLET 192
#define PAL_TRACER_WHITE  143
#define PAL_TRACER_RED    231
#define PAL_TRACER_SAND    34
#define PAL_TRACER_GREEN  255
#define PAL_TRACER_CYAN   218

#define PAL_HULL_STEEL    128
#define PAL_HULL_STEEL_T  136
#define PAL_HULL_IRON     125
#define PAL_HULL_IRON_T   18
#define PAL_HULL_PALE     137
#define PAL_HULL_PALE_T   141
#define PAL_HULL_DARK     119
#define PAL_HULL_DARK_T   127
#define PAL_JOINT         105

//-------------------------------------------------------------------------------------------------

static const tMechaMechDef s_aMechDefs[] = {

//-------------------------------------------------------------------------------------------------
{
  .szName = "LANCER", .szClass = "LINE ASSAULT",
  .fGrip = MECHA_MPS(90.0f), .fDriveAccel = MECHA_MPS(70.0f),
  .fBrake = MECHA_MPS(60.0f),
  .fBuildShoulder = 1.00f, .fBuildTorso = 1.00f, .fBuildLimb = 1.00f,
  .fBuildHead = 1.00f, .fBuildGun = 1.00f,
  .fHeight = MECHA_M(14.0f), .fRadius = MECHA_M(3.2f), .fMass = 1.0f,
  .fArmour = 1000.0f,
  .fWalkSpeed = MECHA_MPS(20.0f), .fDashSpeed = MECHA_MPS(56.0f),
  .fAirSpeed = MECHA_MPS(34.0f), .fTurnRate = (float)MECHA_DEG(200),
  .fJumpVelocity = MECHA_MPS(27.0f),
  .iBoostMax = 1000, .iBoostDashDrain = 340, .iBoostJumpCost = 90,
  .iBoostJumpDrain = 260, .iBoostRegen = 150, .iBoostGuardRegen = 460,
  .iDashTicks = MECHA_SEC(0.75f), .iLandTicks = MECHA_SEC(0.28f),
  .abyPalette = { PAL_HULL_STEEL, PAL_HULL_STEEL_T, PAL_JOINT, PAL_TRACER_CYAN },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "SCATTER", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_AMBER, .iSpreadAngle = MECHA_DEG(4),
        .fSpeed = MECHA_MPS(120.0f), .fDamage = 32.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 90, .iAmmo = 6, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 14, .fStagger = 8.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.1f },
      [MECHA_STANCE_GUARD] = { .szName = "SCATTER LONG", .byKind = MECHA_PROJ_BULLET,
        .byCount = 5, .byPalette = PAL_TRACER_AMBER, .iSpreadAngle = MECHA_DEG(2),
        .fSpeed = MECHA_MPS(155.0f), .fDamage = 30.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 130, .iAmmo = 4, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 26, .fStagger = 9.0f,
        .fMuzzleHeight = 0.44f, .fMuzzleSide = -1.1f },
      [MECHA_STANCE_DASH] = { .szName = "SCATTER RUN", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_AMBER, .iSpreadAngle = MECHA_DEG(7),
        .fSpeed = MECHA_MPS(110.0f), .fDamage = 26.0f, .fRadius = MECHA_M(0.8f),
        .iLifeTicks = 70, .iAmmo = 8, .iReloadTicks = MECHA_SEC(1.8f),
        .iRecoveryTicks = 8, .fStagger = 5.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.1f },
      [MECHA_STANCE_JUMP] = { .szName = "SCATTER RAIN", .byKind = MECHA_PROJ_ARC,
        .byCount = 4, .byPalette = PAL_TRACER_AMBER, .iSpreadAngle = MECHA_DEG(6),
        .fSpeed = MECHA_MPS(75.0f), .fDamage = 28.0f, .fRadius = MECHA_M(1.0f),
        .fArcGravity = MECHA_MPS(30.0f), .iLifeTicks = 150, .iAmmo = 5,
        .iReloadTicks = MECHA_SEC(2.2f), .iRecoveryTicks = 18, .fStagger = 7.0f,
        .fMuzzleHeight = 0.58f, .fMuzzleSide = -1.1f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "LANCE RIFLE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(190.0f), .fDamage = 78.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 150, .iAmmo = 4, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 22, .fStagger = 26.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "LANCE CHARGE", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(420.0f), .fDamage = 148.0f, .fRadius = MECHA_M(1.6f),
        .iLifeTicks = 60, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.6f),
        .iRecoveryTicks = 46, .fStagger = 62.0f,
        .fMuzzleHeight = 0.52f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "LANCE SNAP", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(2),
        .fSpeed = MECHA_MPS(175.0f), .fDamage = 44.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 110, .iAmmo = 5, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 12, .fStagger = 14.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "LANCE DIVE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(3),
        .fSpeed = MECHA_MPS(165.0f), .fDamage = 52.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 120, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 20, .fStagger = 20.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "ARC BOMB", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(95.0f), .fDamage = 96.0f, .fRadius = MECHA_M(1.4f),
        .fBlastRadius = MECHA_M(11.0f), .fArcGravity = MECHA_MPS(32.0f),
        .iLifeTicks = 200, .iAmmo = 3, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 30, .fStagger = 46.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = 1.1f },
      [MECHA_STANCE_GUARD] = { .szName = "SEED MINE", .byKind = MECHA_PROJ_MINE,
        .byCount = 2, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(14),
        .fSpeed = MECHA_MPS(48.0f), .fDamage = 110.0f, .fRadius = MECHA_M(2.0f),
        .fBlastRadius = MECHA_M(13.0f), .fArcGravity = MECHA_MPS(34.0f),
        .iLifeTicks = MECHA_SEC(9.0f), .iAmmo = 2, .iReloadTicks = MECHA_SEC(4.0f),
        .iRecoveryTicks = 26, .fStagger = 54.0f,
        .fMuzzleHeight = 0.36f, .fMuzzleSide = 1.1f },
      [MECHA_STANCE_DASH] = { .szName = "RAM BLADE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(58.0f), .fDamage = 132.0f, .fRadius = MECHA_M(5.0f),
        .iLifeTicks = 16, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 26, .fStagger = 74.0f,
        .fMuzzleHeight = 0.55f, .fMuzzleSide = 1.1f },
      [MECHA_STANCE_JUMP] = { .szName = "CLUSTER DROP", .byKind = MECHA_PROJ_ARC,
        .byCount = 3, .byPalette = PAL_TRACER_ORANGE, .iSpreadAngle = MECHA_DEG(9),
        .fSpeed = MECHA_MPS(58.0f), .fDamage = 70.0f, .fRadius = MECHA_M(1.4f),
        .fBlastRadius = MECHA_M(9.0f), .fArcGravity = MECHA_MPS(38.0f),
        .iLifeTicks = 180, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.4f),
        .iRecoveryTicks = 24, .fStagger = 40.0f,
        .fMuzzleHeight = 0.30f, .fMuzzleSide = 1.1f },
    },
  },
},

//-------------------------------------------------------------------------------------------------
{
  .szName = "SJ Mk.IV", .szClass = "SIEGE PLATFORM",
  /* Carries its mass: slow to gather speed, slower to shed it, and it
   * slides a long way out of anything taken quickly. */
  .fGrip = MECHA_MPS(34.0f), .fDriveAccel = MECHA_MPS(30.0f),
  .fBrake = MECHA_MPS(24.0f),
  /* The bruiser: everything wide, everything heavy, and a gun on each
   * arm you could not possibly run with. */
  .fBuildShoulder = 1.50f, .fBuildTorso = 1.30f, .fBuildLimb = 1.34f,
  .fBuildHead = 0.78f, .fBuildGun = 2.00f,
  .fHeight = MECHA_M(17.0f), .fRadius = MECHA_M(4.2f), .fMass = 1.7f,
  .fArmour = 1450.0f,
  .fWalkSpeed = MECHA_MPS(13.0f), .fDashSpeed = MECHA_MPS(43.0f),
  .fAirSpeed = MECHA_MPS(24.0f), .fTurnRate = (float)MECHA_DEG(140),
  .fJumpVelocity = MECHA_MPS(20.0f),
  .iBoostMax = 1150, .iBoostDashDrain = 400, .iBoostJumpCost = 140,
  .iBoostJumpDrain = 330, .iBoostRegen = 130, .iBoostGuardRegen = 420,
  .iDashTicks = MECHA_SEC(0.60f), .iLandTicks = MECHA_SEC(0.45f),
  .abyPalette = { PAL_HULL_IRON, PAL_HULL_IRON_T, PAL_JOINT, PAL_TRACER_ORANGE },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "AUTOCANNON", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_SAND, .iSpreadAngle = MECHA_DEG(2),
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 30.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 110, .iAmmo = 10, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 9, .fStagger = 7.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = -1.2f },
      [MECHA_STANCE_GUARD] = { .szName = "SUPPRESS", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_SAND, .iSpreadAngle = MECHA_DEG(3),
        .fSpeed = MECHA_MPS(160.0f), .fDamage = 34.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 150, .iAmmo = 12, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 10, .fStagger = 9.0f,
        .fMuzzleHeight = 0.46f, .fMuzzleSide = -1.2f },
      [MECHA_STANCE_DASH] = { .szName = "BURST", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_SAND, .iSpreadAngle = MECHA_DEG(6),
        .fSpeed = MECHA_MPS(125.0f), .fDamage = 26.0f, .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 80, .iAmmo = 8, .iReloadTicks = MECHA_SEC(2.2f),
        .iRecoveryTicks = 8, .fStagger = 6.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = -1.2f },
      [MECHA_STANCE_JUMP] = { .szName = "FLAK", .byKind = MECHA_PROJ_BULLET,
        .byCount = 5, .byPalette = PAL_TRACER_SAND, .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(115.0f), .fDamage = 24.0f, .fRadius = MECHA_M(1.1f),
        .fBlastRadius = MECHA_M(5.0f), .iLifeTicks = 90, .iAmmo = 4,
        .iReloadTicks = MECHA_SEC(2.8f), .iRecoveryTicks = 16, .fStagger = 10.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.2f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "SIEGE SHELL", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(150.0f), .fDamage = 118.0f, .fRadius = MECHA_M(1.5f),
        .fBlastRadius = MECHA_M(9.0f), .iLifeTicks = 160, .iAmmo = 3,
        .iReloadTicks = MECHA_SEC(3.2f), .iRecoveryTicks = 34, .fStagger = 52.0f,
        .fMuzzleHeight = 0.72f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "SIEGE LANCE", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(400.0f), .fDamage = 205.0f, .fRadius = MECHA_M(2.2f),
        .iLifeTicks = 70, .iAmmo = 1, .iReloadTicks = MECHA_SEC(5.0f),
        .iRecoveryTicks = 70, .fStagger = 96.0f,
        .fMuzzleHeight = 0.50f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "ROLL SHELL", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 66.0f, .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(6.0f), .iLifeTicks = 110, .iAmmo = 3,
        .iReloadTicks = MECHA_SEC(2.6f), .iRecoveryTicks = 20, .fStagger = 28.0f,
        .fMuzzleHeight = 0.72f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 2, .byPalette = PAL_TRACER_ORANGE, .iSpreadAngle = MECHA_DEG(7),
        .fSpeed = MECHA_MPS(70.0f), .fDamage = 92.0f, .fRadius = MECHA_M(1.6f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(30.0f),
        .iLifeTicks = 200, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.6f),
        .iRecoveryTicks = 28, .fStagger = 48.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "POD SALVO", .byKind = MECHA_PROJ_HOMING,
        .byCount = 4, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(11),
        .fSpeed = MECHA_MPS(72.0f), .fDamage = 42.0f, .fRadius = MECHA_M(1.1f),
        .fBlastRadius = MECHA_M(6.0f), .iLifeTicks = 190, .iAmmo = 2,
        .iReloadTicks = MECHA_SEC(3.4f), .iRecoveryTicks = 24,
        .iHomingRate = MECHA_DEG(2.6f), .fStagger = 18.0f,
        .fMuzzleHeight = 0.80f, .fMuzzleSide = 1.2f },
      [MECHA_STANCE_GUARD] = { .szName = "POD STORM", .byKind = MECHA_PROJ_HOMING,
        .byCount = 8, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(8),
        .fSpeed = MECHA_MPS(64.0f), .fDamage = 38.0f, .fRadius = MECHA_M(1.1f),
        .fBlastRadius = MECHA_M(6.0f), .iLifeTicks = 230, .iAmmo = 1,
        .iReloadTicks = MECHA_SEC(5.2f), .iRecoveryTicks = 44,
        .iHomingRate = MECHA_DEG(3.2f), .fStagger = 16.0f,
        .fMuzzleHeight = 0.58f, .fMuzzleSide = 1.2f },
      [MECHA_STANCE_DASH] = { .szName = "SHOULDER RAM", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(46.0f), .fDamage = 158.0f, .fRadius = MECHA_M(6.0f),
        .iLifeTicks = 20, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 34, .fStagger = 88.0f,
        .fMuzzleHeight = 0.50f, .fMuzzleSide = 1.2f },
      [MECHA_STANCE_JUMP] = { .szName = "MINE FIELD", .byKind = MECHA_PROJ_MINE,
        .byCount = 4, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(20),
        .fSpeed = MECHA_MPS(40.0f), .fDamage = 88.0f, .fRadius = MECHA_M(2.0f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(34.0f),
        .iLifeTicks = MECHA_SEC(10.0f), .iAmmo = 1, .iReloadTicks = MECHA_SEC(5.0f),
        .iRecoveryTicks = 30, .fStagger = 50.0f,
        .fMuzzleHeight = 0.34f, .fMuzzleSide = 1.2f },
    },
  },
},

//-------------------------------------------------------------------------------------------------
{
  .szName = "Exos 2000", .szClass = "FAST INTERCEPT",
  /* Almost no weight to fight: changes direction nearly on the spot. */
  .fGrip = MECHA_MPS(190.0f), .fDriveAccel = MECHA_MPS(150.0f),
  .fBrake = MECHA_MPS(130.0f),
  /* All silhouette and no mass: narrow shoulders, thin legs, and a head
   * that actually clears them. */
  .fBuildShoulder = 0.78f, .fBuildTorso = 0.80f, .fBuildLimb = 0.72f,
  .fBuildHead = 1.20f, .fBuildGun = 0.66f,
  .fHeight = MECHA_M(12.5f), .fRadius = MECHA_M(2.6f), .fMass = 0.7f,
  .fArmour = 780.0f,
  .fWalkSpeed = MECHA_MPS(25.0f), .fDashSpeed = MECHA_MPS(72.0f),
  .fAirSpeed = MECHA_MPS(42.0f), .fTurnRate = (float)MECHA_DEG(260),
  .fJumpVelocity = MECHA_MPS(33.0f),
  .iBoostMax = 900, .iBoostDashDrain = 300, .iBoostJumpCost = 70,
  .iBoostJumpDrain = 210, .iBoostRegen = 175, .iBoostGuardRegen = 520,
  .iDashTicks = MECHA_SEC(0.90f), .iLandTicks = MECHA_SEC(0.20f),
  .abyPalette = { PAL_HULL_PALE, PAL_HULL_PALE_T, PAL_JOINT, PAL_TRACER_VIOLET },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "NEEDLE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_VIOLET, .iSpreadAngle = MECHA_DEG(2),
        .fSpeed = MECHA_MPS(200.0f), .fDamage = 26.0f, .fRadius = MECHA_M(0.6f),
        .iLifeTicks = 110, .iAmmo = 10, .iReloadTicks = MECHA_SEC(1.9f),
        .iRecoveryTicks = 7, .fStagger = 5.0f,
        .fMuzzleHeight = 0.64f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "NEEDLE FOCUS", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_VIOLET,
        .fSpeed = MECHA_MPS(380.0f), .fDamage = 96.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 60, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 30, .fStagger = 34.0f,
        .fMuzzleHeight = 0.46f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_DASH] = { .szName = "NEEDLE STRAFE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_VIOLET, .iSpreadAngle = MECHA_DEG(4),
        .fSpeed = MECHA_MPS(185.0f), .fDamage = 22.0f, .fRadius = MECHA_M(0.6f),
        .iLifeTicks = 90, .iAmmo = 12, .iReloadTicks = MECHA_SEC(1.7f),
        .iRecoveryTicks = 6, .fStagger = 4.0f,
        .fMuzzleHeight = 0.64f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "NEEDLE RAIN", .byKind = MECHA_PROJ_ARC,
        .byCount = 6, .byPalette = PAL_TRACER_VIOLET, .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(95.0f), .fDamage = 24.0f, .fRadius = MECHA_M(0.7f),
        .fArcGravity = MECHA_MPS(26.0f), .iLifeTicks = 140, .iAmmo = 4,
        .iReloadTicks = MECHA_SEC(2.4f), .iRecoveryTicks = 14, .fStagger = 5.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = -1.0f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "ARC BEAM", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(430.0f), .fDamage = 82.0f, .fRadius = MECHA_M(1.2f),
        .iLifeTicks = 55, .iAmmo = 4, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 28.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "ARC PIERCE", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(470.0f), .fDamage = 138.0f, .fRadius = MECHA_M(1.5f),
        .iLifeTicks = 75, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.4f),
        .iRecoveryTicks = 40, .fStagger = 56.0f,
        .fMuzzleHeight = 0.50f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "ARC SNAP", .byKind = MECHA_PROJ_BEAM,
        .byCount = 1, .byPalette = PAL_TRACER_CYAN,
        .fSpeed = MECHA_MPS(430.0f), .fDamage = 54.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 50, .iAmmo = 5, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 10, .fStagger = 16.0f,
        .fMuzzleHeight = 0.70f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "ARC SWEEP", .byKind = MECHA_PROJ_BEAM,
        .byCount = 3, .byPalette = PAL_TRACER_CYAN, .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(400.0f), .fDamage = 48.0f, .fRadius = MECHA_M(1.0f),
        .iLifeTicks = 55, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 22, .fStagger = 18.0f,
        .fMuzzleHeight = 0.66f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "TRACKER", .byKind = MECHA_PROJ_HOMING,
        .byCount = 2, .byPalette = PAL_TRACER_GREEN, .iSpreadAngle = MECHA_DEG(13),
        .fSpeed = MECHA_MPS(88.0f), .fDamage = 52.0f, .fRadius = MECHA_M(1.0f),
        .fBlastRadius = MECHA_M(5.0f), .iLifeTicks = 180, .iAmmo = 3,
        .iReloadTicks = MECHA_SEC(2.8f), .iRecoveryTicks = 18,
        .iHomingRate = MECHA_DEG(4.0f), .fStagger = 20.0f,
        .fMuzzleHeight = 0.78f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "TRACKER LOCK", .byKind = MECHA_PROJ_HOMING,
        .byCount = 4, .byPalette = PAL_TRACER_GREEN, .iSpreadAngle = MECHA_DEG(9),
        .fSpeed = MECHA_MPS(80.0f), .fDamage = 56.0f, .fRadius = MECHA_M(1.0f),
        .fBlastRadius = MECHA_M(6.0f), .iLifeTicks = 220, .iAmmo = 2,
        .iReloadTicks = MECHA_SEC(3.8f), .iRecoveryTicks = 30,
        .iHomingRate = MECHA_DEG(5.0f), .fStagger = 22.0f,
        .fMuzzleHeight = 0.56f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_DASH] = { .szName = "WING BLADE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(70.0f), .fDamage = 104.0f, .fRadius = MECHA_M(4.4f),
        .iLifeTicks = 14, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.0f),
        .iRecoveryTicks = 20, .fStagger = 64.0f,
        .fMuzzleHeight = 0.55f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "TRACKER RAIN", .byKind = MECHA_PROJ_HOMING,
        .byCount = 5, .byPalette = PAL_TRACER_GREEN, .iSpreadAngle = MECHA_DEG(15),
        .fSpeed = MECHA_MPS(76.0f), .fDamage = 40.0f, .fRadius = MECHA_M(1.0f),
        .fBlastRadius = MECHA_M(5.0f), .iLifeTicks = 200, .iAmmo = 2,
        .iReloadTicks = MECHA_SEC(3.4f), .iRecoveryTicks = 22,
        .iHomingRate = MECHA_DEG(4.4f), .fStagger = 16.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 1.0f },
    },
  },
},

//-------------------------------------------------------------------------------------------------
{
  .szName = "Kira Type R", .szClass = "CLOSE QUARTERS",
  .fGrip = MECHA_MPS(120.0f), .fDriveAccel = MECHA_MPS(100.0f),
  .fBrake = MECHA_MPS(85.0f),
  .fBuildShoulder = 0.90f, .fBuildTorso = 0.94f, .fBuildLimb = 0.84f,
  .fBuildHead = 1.06f, .fBuildGun = 0.82f,
  .fHeight = MECHA_M(13.5f), .fRadius = MECHA_M(3.0f), .fMass = 0.9f,
  .fArmour = 900.0f,
  .fWalkSpeed = MECHA_MPS(23.0f), .fDashSpeed = MECHA_MPS(80.0f),
  .fAirSpeed = MECHA_MPS(37.0f), .fTurnRate = (float)MECHA_DEG(230),
  .fJumpVelocity = MECHA_MPS(29.0f),
  .iBoostMax = 1000, .iBoostDashDrain = 330, .iBoostJumpCost = 80,
  .iBoostJumpDrain = 240, .iBoostRegen = 160, .iBoostGuardRegen = 500,
  .iDashTicks = MECHA_SEC(0.80f), .iLandTicks = MECHA_SEC(0.24f),
  .abyPalette = { PAL_HULL_DARK, PAL_HULL_DARK_T, PAL_JOINT, PAL_TRACER_RED },
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      [MECHA_STANCE_STAND] = { .szName = "SIDEARM", .byKind = MECHA_PROJ_BULLET,
        .byCount = 3, .byPalette = PAL_TRACER_AMBER, .iSpreadAngle = MECHA_DEG(3),
        .fSpeed = MECHA_MPS(150.0f), .fDamage = 22.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 80, .iAmmo = 9, .iReloadTicks = MECHA_SEC(1.8f),
        .iRecoveryTicks = 8, .fStagger = 5.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "SIDEARM AIMED", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_AMBER, .iSpreadAngle = MECHA_DEG(1),
        .fSpeed = MECHA_MPS(195.0f), .fDamage = 44.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 140, .iAmmo = 6, .iReloadTicks = MECHA_SEC(2.2f),
        .iRecoveryTicks = 16, .fStagger = 12.0f,
        .fMuzzleHeight = 0.44f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_DASH] = { .szName = "SIDEARM RUN", .byKind = MECHA_PROJ_BULLET,
        .byCount = 2, .byPalette = PAL_TRACER_AMBER, .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(140.0f), .fDamage = 20.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 70, .iAmmo = 10, .iReloadTicks = MECHA_SEC(1.6f),
        .iRecoveryTicks = 6, .fStagger = 4.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = -1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "SIDEARM FAN", .byKind = MECHA_PROJ_BULLET,
        .byCount = 4, .byPalette = PAL_TRACER_AMBER, .iSpreadAngle = MECHA_DEG(6),
        .fSpeed = MECHA_MPS(135.0f), .fDamage = 24.0f, .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 80, .iAmmo = 5, .iReloadTicks = MECHA_SEC(2.2f),
        .iRecoveryTicks = 12, .fStagger = 6.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = -1.0f },
    },
    [MECHA_SLOT_CENTER] = {
      [MECHA_STANCE_STAND] = { .szName = "SABRE SLASH", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(34.0f), .fDamage = 118.0f, .fRadius = MECHA_M(5.2f),
        .iLifeTicks = 14, .iAmmo = 3, .iReloadTicks = MECHA_SEC(1.8f),
        .iRecoveryTicks = 20, .fStagger = 58.0f,
        .fMuzzleHeight = 0.60f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_GUARD] = { .szName = "SABRE RISE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(26.0f), .fDamage = 146.0f, .fRadius = MECHA_M(4.6f),
        .iLifeTicks = 18, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 32, .fStagger = 104.0f,
        .fMuzzleHeight = 0.40f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_DASH] = { .szName = "SABRE LUNGE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(96.0f), .fDamage = 172.0f, .fRadius = MECHA_M(5.6f),
        .iLifeTicks = 26, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 30, .fStagger = 96.0f,
        .fMuzzleHeight = 0.55f, .fMuzzleSide = 0.0f },
      [MECHA_STANCE_JUMP] = { .szName = "SABRE DIVE", .byKind = MECHA_PROJ_MELEE,
        .byCount = 1, .byPalette = PAL_TRACER_WHITE,
        .fSpeed = MECHA_MPS(74.0f), .fDamage = 154.0f, .fRadius = MECHA_M(5.0f),
        .iLifeTicks = 24, .iAmmo = 2, .iReloadTicks = MECHA_SEC(2.8f),
        .iRecoveryTicks = 34, .fStagger = 92.0f,
        .fMuzzleHeight = 0.45f, .fMuzzleSide = 0.0f },
    },
    [MECHA_SLOT_RIGHT] = {
      [MECHA_STANCE_STAND] = { .szName = "SNARE", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_GREEN,
        .fSpeed = MECHA_MPS(90.0f), .fDamage = 62.0f, .fRadius = MECHA_M(1.6f),
        .fBlastRadius = MECHA_M(10.0f), .fArcGravity = MECHA_MPS(30.0f),
        .iLifeTicks = 170, .iAmmo = 3, .iReloadTicks = MECHA_SEC(2.6f),
        .iRecoveryTicks = 22, .fStagger = 70.0f,
        .fMuzzleHeight = 0.58f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_GUARD] = { .szName = "TRIP MINE", .byKind = MECHA_PROJ_MINE,
        .byCount = 3, .byPalette = PAL_TRACER_RED, .iSpreadAngle = MECHA_DEG(18),
        .fSpeed = MECHA_MPS(44.0f), .fDamage = 74.0f, .fRadius = MECHA_M(1.8f),
        .fBlastRadius = MECHA_M(10.0f), .fArcGravity = MECHA_MPS(34.0f),
        .iLifeTicks = MECHA_SEC(8.0f), .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.6f),
        .iRecoveryTicks = 24, .fStagger = 44.0f,
        .fMuzzleHeight = 0.34f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_DASH] = { .szName = "CROSS SLASH", .byKind = MECHA_PROJ_MELEE,
        .byCount = 2, .byPalette = PAL_TRACER_WHITE, .iSpreadAngle = MECHA_DEG(16),
        .fSpeed = MECHA_MPS(88.0f), .fDamage = 92.0f, .fRadius = MECHA_M(4.6f),
        .iLifeTicks = 22, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.0f),
        .iRecoveryTicks = 32, .fStagger = 62.0f,
        .fMuzzleHeight = 0.55f, .fMuzzleSide = 1.0f },
      [MECHA_STANCE_JUMP] = { .szName = "DROP CHARGE", .byKind = MECHA_PROJ_ARC,
        .byCount = 2, .byPalette = PAL_TRACER_ORANGE, .iSpreadAngle = MECHA_DEG(8),
        .fSpeed = MECHA_MPS(52.0f), .fDamage = 80.0f, .fRadius = MECHA_M(1.5f),
        .fBlastRadius = MECHA_M(11.0f), .fArcGravity = MECHA_MPS(40.0f),
        .iLifeTicks = 170, .iAmmo = 2, .iReloadTicks = MECHA_SEC(3.2f),
        .iRecoveryTicks = 26, .fStagger = 46.0f,
        .fMuzzleHeight = 0.30f, .fMuzzleSide = 1.0f },
    },
  },
},

//-------------------------------------------------------------------------------------------------
{
  /*
   * The odd one out, and deliberately so.
   *
   * It is a race car with a handgun on it: no legs, no arms, no boost and
   * no jump. What it has instead is speed it does not have to spend
   * anything on and a body small enough to be hard to hit, and the price is
   * that it can only point where it is driving. There is no auto-turn on
   * this machine at any range, so the only way it holds a lock is to drive
   * at somebody and keep them in the middle of the screen -- which is also
   * the only way it lines up its one gun.
   *
   * That gun is the whole armament. All three triggers are the same weapon,
   * because there is only one of it: one shot in the chamber, two and a
   * half seconds to put another one in, and enough behind it that landing
   * one matters. Firing shoves the car. And with no melee row at all, its
   * answer at close quarters is to drive into you, which is the other thing
   * a car is for.
   */
  .szName = "ZIZIN KLR 330", .szClass = "GUN CAR",
  .bWheeled = true,
  .fGrip = MECHA_MPS(11.0f), .fDriveAccel = MECHA_MPS(34.0f),
  .fBrake = MECHA_MPS(72.0f),
  .fSteerFloor = MECHA_MPS(4.0f),
  .fRamDamage = 3.4f, .fRamSpeed = MECHA_MPS(28.0f),
  .fRecoilPush = MECHA_MPS(13.0f),
  .fBuildShoulder = 1.00f, .fBuildTorso = 1.00f, .fBuildLimb = 1.00f,
  .fBuildHead = 1.00f, .fBuildGun = 1.00f,
  /* A sixth of a machine's height, and about as wide as it is tall, which
   * is what a car is. */
  .fHeight = MECHA_M(2.4f), .fRadius = MECHA_M(2.0f), .fMass = 0.55f,
  .fArmour = 720.0f,
  .fWalkSpeed = MECHA_MPS(66.0f), .fDashSpeed = MECHA_MPS(66.0f),
  /*
   * Flat out it barely turns and just off a standstill it spins on the
   * spot: 57 degrees a second times the steering bonus, which is seven at
   * rest. That is Whiplash's own curve -- 72 units of lock a tick at 36 Hz
   * with the bonus on top -- and it is why a car has to be slowed into a
   * corner rather than steered round one.
   */
  .fAirSpeed = MECHA_MPS(66.0f), .fTurnRate = (float)MECHA_DEG(57),
  .fJumpVelocity = 0.0f,
  .iBoostMax = 1000, .iBoostDashDrain = 0, .iBoostJumpCost = 0,
  .iBoostJumpDrain = 0, .iBoostRegen = 1000, .iBoostGuardRegen = 1000,
  .iDashTicks = MECHA_SEC(1.0f), .iLandTicks = MECHA_SEC(0.10f),
  .abyPalette = { PAL_HULL_PALE, PAL_HULL_DARK, PAL_JOINT, PAL_TRACER_AMBER },
  /*
   * One gun, three loads, nine rounds between them.
   *
   * The Zizin carries a single oversized handgun and the three triggers
   * are three things to put through it, not three weapons: a magazine of
   * nine that every trigger draws from, and one long reload when it runs
   * dry. So the choice is never which gun to use, it is what to spend the
   * next round on -- and spending it badly costs the same as spending it
   * well.
   *
   * Each slot is the same in every stance, because a car has no stances:
   * it is always simply driving.
   */
  .aWeapons = {
    [MECHA_SLOT_LEFT] = {
      /* Buckshot. Seven pellets across five degrees and gone in half a
       * second, so it is devastating at ramming distance and litter at
       * any other -- which suits a machine whose other close-quarters
       * answer is to drive into you. */
      [MECHA_STANCE_STAND] = { .szName = "KLR BUCKSHOT", .byKind = MECHA_PROJ_BULLET,
        .byCount = 7, .byPalette = PAL_TRACER_AMBER,
        .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(210.0f), .fDamage = 24.0f,
        .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 34, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 11.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_GUARD] = { .szName = "KLR BUCKSHOT", .byKind = MECHA_PROJ_BULLET,
        .byCount = 7, .byPalette = PAL_TRACER_AMBER,
        .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(210.0f), .fDamage = 24.0f,
        .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 34, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 11.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_DASH] = { .szName = "KLR BUCKSHOT", .byKind = MECHA_PROJ_BULLET,
        .byCount = 7, .byPalette = PAL_TRACER_AMBER,
        .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(210.0f), .fDamage = 24.0f,
        .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 34, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 11.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_JUMP] = { .szName = "KLR BUCKSHOT", .byKind = MECHA_PROJ_BULLET,
        .byCount = 7, .byPalette = PAL_TRACER_AMBER,
        .iSpreadAngle = MECHA_DEG(5),
        .fSpeed = MECHA_MPS(210.0f), .fDamage = 24.0f,
        .fRadius = MECHA_M(0.9f),
        .iLifeTicks = 34, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 20, .fStagger = 11.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
    },
    [MECHA_SLOT_CENTER] = {
      /* The lance. One round, no spread, five hundred metres a second
       * and a long look down the barrel before the car can do anything
       * else. This is the shot the whole machine is built around: keep
       * somebody centred in the reticle at range and take their head
       * off. */
      [MECHA_STANCE_STAND] = { .szName = "KLR LANCE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_AMBER,
        .fSpeed = MECHA_MPS(520.0f), .fDamage = 112.0f,
        .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 150, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 34, .fStagger = 58.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_GUARD] = { .szName = "KLR LANCE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_AMBER,
        .fSpeed = MECHA_MPS(520.0f), .fDamage = 112.0f,
        .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 150, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 34, .fStagger = 58.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_DASH] = { .szName = "KLR LANCE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_AMBER,
        .fSpeed = MECHA_MPS(520.0f), .fDamage = 112.0f,
        .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 150, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 34, .fStagger = 58.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_JUMP] = { .szName = "KLR LANCE", .byKind = MECHA_PROJ_BULLET,
        .byCount = 1, .byPalette = PAL_TRACER_AMBER,
        .fSpeed = MECHA_MPS(520.0f), .fDamage = 112.0f,
        .fRadius = MECHA_M(0.7f),
        .iLifeTicks = 150, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 34, .fStagger = 58.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
    },
    [MECHA_SLOT_RIGHT] = {
      /* And a shell lobbed over whatever is in the way. Slow enough to
       * be dodged if it is seen coming, which is the price of it not
       * needing line of sight, and it does not care whether it hits --
       * twelve metres of blast finds people behind cover. */
      [MECHA_STANCE_STAND] = { .szName = "KLR MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(92.0f), .fDamage = 74.0f,
        .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(32.0f),
        .iLifeTicks = 200, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 42, .fStagger = 40.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_GUARD] = { .szName = "KLR MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(92.0f), .fDamage = 74.0f,
        .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(32.0f),
        .iLifeTicks = 200, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 42, .fStagger = 40.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_DASH] = { .szName = "KLR MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(92.0f), .fDamage = 74.0f,
        .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(32.0f),
        .iLifeTicks = 200, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 42, .fStagger = 40.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
      [MECHA_STANCE_JUMP] = { .szName = "KLR MORTAR", .byKind = MECHA_PROJ_ARC,
        .byCount = 1, .byPalette = PAL_TRACER_ORANGE,
        .fSpeed = MECHA_MPS(92.0f), .fDamage = 74.0f,
        .fRadius = MECHA_M(1.3f),
        .fBlastRadius = MECHA_M(12.0f), .fArcGravity = MECHA_MPS(32.0f),
        .iLifeTicks = 200, .iAmmo = MECHA_CAR_MAGAZINE, .iReloadTicks = MECHA_SEC(2.4f),
        .iRecoveryTicks = 42, .fStagger = 40.0f,
        .fMuzzleHeight = 0.62f, .fMuzzleSide = 0.85f },
    },
  }
},

};

//-------------------------------------------------------------------------------------------------

#define MECHA_DEF_COUNT ((int)(sizeof(s_aMechDefs) / sizeof(s_aMechDefs[0])))

//-------------------------------------------------------------------------------------------------

int mecha_def_count(void)
{
  return MECHA_DEF_COUNT;
}

//-------------------------------------------------------------------------------------------------

const tMechaMechDef *mecha_def_get(int iDefIdx)
{
  if (iDefIdx < 0)
    iDefIdx = 0;
  return &s_aMechDefs[iDefIdx % MECHA_DEF_COUNT];
}
