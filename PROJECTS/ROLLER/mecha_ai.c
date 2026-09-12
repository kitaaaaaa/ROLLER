#include "mecha_ai.h"

#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_sim.h"

#include <math.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------
/*
 * The computer pilot. Holds no state: every decision comes from the world it
 * is handed plus the shared RNG, so a match replays exactly from its seed.
 * Anything resembling memory is derived from the tick counter.
 */
//-------------------------------------------------------------------------------------------------

/* How long the pilot commits to a strafe direction before reconsidering. */
#define MECHA_AI_STRAFE_TICKS 48

#define MECHA_AI_DODGE_LOOKAHEAD 0.9f

/* A shot this close to passing through us is worth boost to avoid. The same
 * at every skill level; scaling it ran the ladder backwards. [AI-01] */
#define MECHA_AI_DODGE_MARGIN (4.0f * MECHA_METRE)

//-------------------------------------------------------------------------------------------------
/*
 * What separates the skill levels. Measured, not chosen by taste: aim error
 * is the lever that works, reaction time is presentation rather than
 * strength. [AI-02]
 */
typedef struct
{
  int iReactionTicks;  /* a shot is invisible to the pilot until this old */
  int iAimError;       /* peak error either side of the firing solution */
  int iTriggerOdds;    /* 1-in-N per tick of committing to a shot */
  int iTurnPercent;    /* how hard it pushes the stick to point the machine */
} tMechaAiProfile;

static const tMechaAiProfile s_aAiProfiles[MECHA_AI_SKILL_COUNT] = {
  [MECHA_AI_ROOKIE]  = { MECHA_SEC(0.30f), MECHA_DEG(14), 8, 55 },
  [MECHA_AI_VETERAN] = { MECHA_SEC(0.20f), MECHA_DEG(9),  3, 80 },
  [MECHA_AI_ACE]     = { MECHA_SEC(0.15f), 0,             1, 100 },
};

static const tMechaAiProfile *mecha_ai_profile(const tMechaWorld *pWorld)
{
  int iSkill = (int)pWorld->byAiSkill;

  if (iSkill < 0 || iSkill >= MECHA_AI_SKILL_COUNT)
    iSkill = MECHA_AI_VETERAN;
  return &s_aAiProfiles[iSkill];
}

/* Only shoot when the shoulders are roughly square to the target; the lock
 * turns the mech at a fixed rate and firing early just sprays. */
#define MECHA_AI_FIRE_CONE MECHA_DEG(11)

//-------------------------------------------------------------------------------------------------

/* The distance this weapon wants to be used at. */
static float mecha_ai_weapon_range(const tMechaWeaponDef *pWeapon)
{
  switch (pWeapon->byKind) {
  case MECHA_PROJ_MELEE:  return MECHA_M(16.0f);
  case MECHA_PROJ_MINE:   return MECHA_M(30.0f);
  case MECHA_PROJ_ARC:    return MECHA_M(58.0f);
  case MECHA_PROJ_HOMING: return MECHA_M(82.0f);
  case MECHA_PROJ_BEAM:   return MECHA_M(105.0f);
  default:                return MECHA_M(70.0f);
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * How far ahead a machine has to look, which is how far it takes to stop:
 * braking distance v^2/2a off its own grip, or a linear guess, whichever is
 * further, plus a stride of reaction. [AI-03]
 */
static float mecha_ai_stopping_look(const tMechaMechDef *pDef, float fSpeed,
                                    float fScale)
{
  float fBrake = pDef->fGrip > 0.0f ? pDef->fGrip : MECHA_MPS(30.0f);
  float fStop = fSpeed * fSpeed / (2.0f * fBrake);
  float fGuess = fScale * fSpeed;

  return MECHA_AI_FOOTING_WALK + (fStop > fGuess ? fStop : fGuess);
}

//-------------------------------------------------------------------------------------------------

/* Is there still an arena fLook metres that way? The direction need not be
 * normalised; a zero-length one is going nowhere and so is always fine. */
static bool mecha_ai_footing_clear(const tMechaWorld *pWorld,
                                   const tMechaMech *pSelf, float fDirX,
                                   float fDirZ, float fLook, bool bEdgeKills)
{
  float fLen = mecha_length2(fDirX, fDirZ);
  float fX;
  float fZ;

  if (fLen <= 0.01f)
    return true;

  fX = pSelf->fX + fDirX / fLen * fLook;
  fZ = pSelf->fZ + fDirZ / fLen * fLook;

  if ((mecha_arena_surface(&pWorld->arena, fX, fZ) & MECHA_SURF_PIT) != 0)
    return false;

  return !bEdgeKills || mecha_arena_contains(&pWorld->arena, fX, fZ);
}

//-------------------------------------------------------------------------------------------------

/*
 * Somewhere to go instead: turn away, widening until the ground comes back.
 * On a roof with a hole in it, straight back the way you came is as likely
 * to be the pit as the edge was, so "away" has to be looked at. False when
 * every way out is as bad as the way in; the caller then reverses.
 */
static bool mecha_ai_footing_escape(const tMechaWorld *pWorld,
                                    const tMechaMech *pSelf, float fDirX,
                                    float fDirZ, float fLook, bool bEdgeKills,
                                    float *pfOutX, float *pfOutZ)
{
  static const int aiTurn[] = { 8192, 6144, 10240, 4096, 12288 };
  float fLen = mecha_length2(fDirX, fDirZ);
  size_t i;

  if (fLen <= 0.01f)
    return false;

  for (i = 0; i < sizeof(aiTurn) / sizeof(aiTurn[0]); i++) {
    float fCos = mecha_cos(aiTurn[i]);
    float fSin = mecha_sin(aiTurn[i]);
    float fTryX = (fDirX * fCos - fDirZ * fSin) / fLen;
    float fTryZ = (fDirX * fSin + fDirZ * fCos) / fLen;

    if (mecha_ai_footing_clear(pWorld, pSelf, fTryX, fTryZ, fLook,
                               bEdgeKills)) {
      *pfOutX = fTryX;
      *pfOutZ = fTryZ;
      return true;
    }
  }

  return false;
}

//-------------------------------------------------------------------------------------------------

/* The range the mech as a whole wants to fight at, taken from whatever its
 * main weapon happens to be. A blade mech ends up wanting to be on top of
 * you; a siege platform wants the far side of the arena. */
static float mecha_ai_preferred_range(const tMechaMechDef *pDef)
{
  return mecha_ai_weapon_range(
      &pDef->aWeapons[MECHA_SLOT_CENTER][MECHA_STANCE_STAND]);
}

//-------------------------------------------------------------------------------------------------

static bool mecha_ai_has_line(const tMechaWorld *pWorld, int iMechIdx,
                              int iTargetIdx)
{
  const tMechaMech *pSelf = &pWorld->aMechs[iMechIdx];
  const tMechaMech *pTarget = &pWorld->aMechs[iTargetIdx];
  const tMechaMechDef *pDef = mecha_def_get((int)pSelf->byDefIdx);

  return !mecha_arena_trace_segment(
      &pWorld->arena,
      pSelf->fX, pSelf->fY + pDef->fHeight * 0.7f, pSelf->fZ,
      pTarget->fX, mecha_mech_centre_height(pWorld, iTargetIdx), pTarget->fZ,
      NULL, NULL, NULL);
}

//-------------------------------------------------------------------------------------------------

/* Is anything hostile about to pass through us? Returns the sign of the
 * evasive strafe (-1 left, +1 right), or 0 when nothing needs dodging. */
static int mecha_ai_incoming(const tMechaWorld *pWorld, int iMechIdx,
                             const tMechaAiProfile *pProfile)
{
  const tMechaMech *pSelf = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_def_get((int)pSelf->byDefIdx);
  float fBestTime = MECHA_AI_DODGE_LOOKAHEAD;
  int iBestSign = 0;
  int i;

  for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
    const tMechaProjectile *pShot = &pWorld->aProjectiles[i];
    float fRelX;
    float fRelZ;
    float fSpeedSq;
    float fTime;
    float fMissX;
    float fMissZ;
    float fMiss;
    float fSideX;
    float fSideZ;

    if (!pShot->bActive || (int)pShot->byOwner == iMechIdx)
      continue;
    if (pWorld->aMechs[pShot->byOwner].byTeam == pSelf->byTeam)
      continue;
    /* Nobody sees a shot the instant it leaves the muzzle. Without this the
     * pilot is already stepping aside on the tick it was fired, which is
     * what made it untouchable. */
    if (pShot->iAge < pProfile->iReactionTicks)
      continue;

    fRelX = pSelf->fX - pShot->fX;
    fRelZ = pSelf->fZ - pShot->fZ;
    fSpeedSq = pShot->fVelX * pShot->fVelX + pShot->fVelZ * pShot->fVelZ;
    if (fSpeedSq < 1.0f)
      continue;

    /* When the shot is closest to us, measured along its own flight. */
    fTime = (fRelX * pShot->fVelX + fRelZ * pShot->fVelZ) / fSpeedSq;
    if (fTime <= 0.0f || fTime >= fBestTime)
      continue;

    fMissX = fRelX - pShot->fVelX * fTime;
    fMissZ = fRelZ - pShot->fVelZ * fTime;
    fMiss = mecha_length2(fMissX, fMissZ);
    if (fMiss > pDef->fRadius + pShot->fRadius + MECHA_AI_DODGE_MARGIN)
      continue;

    /* Step off the line of flight, towards whichever side we are already
     * drifting -- crossing in front of the shot is how you get hit. */
    fSideX = mecha_cos(pSelf->iFacing);
    fSideZ = -mecha_sin(pSelf->iFacing);
    fBestTime = fTime;
    iBestSign = (fMissX * fSideX + fMissZ * fSideZ) >= 0.0f ? 1 : -1;
  }
  return iBestSign;
}

//-------------------------------------------------------------------------------------------------

/* Which trigger to pull, or -1 to hold fire. */
static int mecha_ai_choose_weapon(const tMechaWorld *pWorld, int iMechIdx,
                                  float fDistance, bool bHasLine)
{
  const tMechaMech *pSelf = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_def_get((int)pSelf->byDefIdx);
  eMechaStance eStance = mecha_mech_stance(pSelf);
  const tMechaMechDef *pTargetDef =
    (pSelf->iTargetIdx >= 0 && pSelf->iTargetIdx < MECHA_MAX_MECHS)
      ? mecha_def_get((int)pWorld->aMechs[pSelf->iTargetIdx].byDefIdx)
      : NULL;
  float fBestScore = 0.0f;
  int iBest = -1;
  int iSlot;

  for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
    const tMechaWeaponDef *pWeapon = &pDef->aWeapons[iSlot][eStance];
    float fWant;
    float fHits;
    float fScore;

    if (pSelf->aiAmmo[iSlot] <= 0 || pSelf->aiReload[iSlot] > 0)
      continue;
    if (pWeapon->fSpeed <= 0.0f)
      continue;

    /* Direct-fire weapons are worthless through a pillar; lobbed shots and
     * missiles are not. */
    if (!bHasLine && (pWeapon->byKind == MECHA_PROJ_BULLET
                      || pWeapon->byKind == MECHA_PROJ_BEAM
                      || pWeapon->byKind == MECHA_PROJ_MELEE))
      continue;

    if (pWeapon->byKind == MECHA_PROJ_MELEE
        && fDistance > pWeapon->fRadius + pDef->fRadius
                       + pWeapon->fSpeed * (float)pWeapon->iLifeTicks
                         * MECHA_TICK_SECONDS)
      continue;

    /*
     * Damage per second, discounted by distance from the weapon's preferred
     * range and by the share of a spread that actually arrives. [AI-04]
     */
    fWant = mecha_ai_weapon_range(pWeapon);
    fHits = (float)pWeapon->byCount;
    if (pWeapon->byCount > 1 && pWeapon->iSpreadAngle > 0
        && fDistance > 0.0f) {
      float fTarget = 2.0f * (pTargetDef ? pTargetDef->fRadius
                                         : pDef->fRadius);
      float fWidth = 2.0f * fDistance
                     * mecha_sin(pWeapon->iSpreadAngle
                                 * (pWeapon->byCount - 1) / 2);

      if (fWidth > fTarget && fTarget > 0.0f)
        fHits *= fTarget / fWidth;
    }
    fScore = pWeapon->fDamage * fHits
             / (float)(pWeapon->iRecoveryTicks + 1);
    fScore /= 1.0f + fabsf(fDistance - fWant) / fWant;

    if (fScore > fBestScore) {
      fBestScore = fScore;
      iBest = iSlot;
    }
  }
  return iBest;
}

//-------------------------------------------------------------------------------------------------

void mecha_ai_think(tMechaWorld *pWorld, int iMechIdx, tMechaInput *pOut)
{
  const tMechaMech *pSelf;
  const tMechaMech *pTarget;
  const tMechaMechDef *pDef;
  int iTargetIdx;
  int iDodge;
  int iSlot;
  int iBearing;
  int iOff;
  /* Set by the footwork below when the machine is about to be somewhere
   * there is no arena. */
  bool bFooting = false;
  const tMechaAiProfile *pProfile;
  float fDistance;
  float fPreferred;
  float fBoost;
  bool bHasLine;
  bool bAmmoLeft = false;

  if (!pOut)
    return;
  memset(pOut, 0, sizeof(*pOut));
  if (!pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return;

  pSelf = &pWorld->aMechs[iMechIdx];
  if (!mecha_mech_alive(pSelf))
    return;
  pDef = mecha_def_get((int)pSelf->byDefIdx);
  pProfile = mecha_ai_profile(pWorld);

  /* The lock is refreshed after the pilot has already decided, so on the
   * opening tick it is still unset. */
  iTargetIdx = pSelf->iTargetIdx;
  if (iTargetIdx < 0 || iTargetIdx >= MECHA_MAX_MECHS
      || !mecha_mech_alive(&pWorld->aMechs[iTargetIdx]))
    iTargetIdx = mecha_sim_nearest_enemy(pWorld, iMechIdx);
  if (iTargetIdx < 0)
    return;

  pTarget = &pWorld->aMechs[iTargetIdx];
  fDistance = mecha_length2(pTarget->fX - pSelf->fX, pTarget->fZ - pSelf->fZ);
  fPreferred = mecha_ai_preferred_range(pDef);
  fBoost = mecha_mech_boost_fraction(pWorld, iMechIdx);
  bHasLine = mecha_ai_has_line(pWorld, iMechIdx, iTargetIdx);

  for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
    if (pSelf->aiAmmo[iSlot] > 0 && pSelf->aiReload[iSlot] <= 0)
      bAmmoLeft = true;
  }

  /* --- driving, for the one machine that does ---------------------------- */

  /*
   * A car gets a different pilot: no footwork, no gauge, no dodging worth
   * the name. It can only point where it is going, so aiming and closing are
   * the same act and the answer to most things is throttle and steering.
   */
  if (pDef->bWheeled) {
    int iDriveBearing = mecha_atan2_angle(pTarget->fX - pSelf->fX,
                                          pTarget->fZ - pSelf->fZ);
    int iDriveOff = mecha_angle_delta(pSelf->iFacing, iDriveBearing);
    float fSpeed = mecha_length2(pSelf->fVelX, pSelf->fVelZ);
    bool bClear = true;

    pOut->iTurn = iDriveOff >= 0 ? pProfile->iTurnPercent
                                 : -pProfile->iTurnPercent;
    /* Nearly straight at them: stop sawing at the wheel, or the gun never
     * settles long enough to be worth firing. */
    if (iDriveOff < MECHA_AI_DRIVE_STRAIGHT
        && iDriveOff > -MECHA_AI_DRIVE_STRAIGHT)
      pOut->iTurn = 0;

    /* A car cannot step back, so all it can do about an edge is lift off
     * and turn. */
    if (fSpeed > 0.01f) {
      bClear = mecha_ai_footing_clear(
          pWorld, pSelf, pSelf->fVelX, pSelf->fVelZ,
          mecha_ai_stopping_look(pDef, fSpeed, MECHA_AI_FOOTING_CANCEL),
          pWorld->arena.byShape == MECHA_ARENA_OPEN);
    }

    if (!bClear) {
      /* Hard over and off the gas. Which way it turns hardly matters as
       * long as it is away from straight ahead. */
      pOut->bGuard = true;
      pOut->iTurn = iDriveOff >= 0 ? 100 : -100;
    } else {
      /* Drive at them. Lifting when the nose is off line is its only
       * steering aid: the wheels bite hardest below top speed. */
      pOut->bDash = iDriveOff < MECHA_AI_DRIVE_LIFT
                    && iDriveOff > -MECHA_AI_DRIVE_LIFT;
      /* Too slow to steer at all is worse than any of it: get moving. */
      if (fSpeed < pDef->fSteerFloor * 1.5f)
        pOut->bDash = true;
    }

    pOut->bJump = false;
    pOut->iMoveX = 0;
    pOut->iMoveZ = 0;
    iBearing = iDriveBearing;
    iOff = iDriveOff < 0 ? -iDriveOff : iDriveOff;
    goto shooting;
  }

  /* --- footwork --------------------------------------------------------- */

  /* Circling direction is held for about a second at a time so the mech
   * commits to an arc instead of jittering on the spot. */
  {
    int iPhase = (pWorld->iTick / MECHA_AI_STRAFE_TICKS) + iMechIdx;

    pOut->iMoveX = (iPhase & 1) ? 100 : -100;
  }

  if (fDistance > fPreferred * 1.25f) {
    pOut->iMoveZ = 100;
    pOut->iMoveX /= 2;
    /* Closing a long gap is what the boost is for. */
    if (fDistance > fPreferred * 1.8f && fBoost > 0.45f)
      pOut->bDash = true;
  } else if (fDistance < fPreferred * 0.55f) {
    pOut->iMoveZ = -100;
    pOut->iMoveX /= 2;
    if (fBoost > 0.6f)
      pOut->bDash = true;
  } else {
    pOut->iMoveZ = (fDistance < fPreferred) ? -25 : 25;
  }

  /* Out of everything, or blind behind cover with only direct-fire weapons:
   * sit down and refill, which is one of the things guard is for. * */
  if ((!bAmmoLeft || (!bHasLine && fBoost < 0.35f))
      && fDistance > fPreferred * 0.8f) {
    pOut->bGuard = true;
    pOut->iMoveX = 0;
    pOut->iMoveZ = 0;
    pOut->bDash = false;
  }

  /* Take the high ground now and then, or hop a wall that is in the way. */
  if (!pOut->bGuard && fBoost > 0.7f
      && mecha_rng_range(&pWorld->rng, 240) == 0)
    pOut->bJump = true;
  if (!bHasLine && fBoost > 0.5f && fDistance < fPreferred
      && mecha_rng_range(&pWorld->rng, 90) == 0)
    pOut->bJump = true;

  /* --- evasion ---------------------------------------------------------- */

  iDodge = mecha_ai_incoming(pWorld, iMechIdx, pProfile);
  if (iDodge != 0 && fBoost > 0.2f) {
    pOut->iMoveX = iDodge * 100;
    pOut->iMoveZ = 0;
    pOut->bDash = true;
    pOut->bGuard = false;
  }

  /* --- holding the lock ------------------------------------------------- */

  iBearing = mecha_atan2_angle(pTarget->fX - pSelf->fX,
                               pTarget->fZ - pSelf->fZ);
  iOff = mecha_angle_delta(pSelf->iFacing, iBearing);
  if (iOff < 0)
    iOff = -iOff;

  /*
   * The pilot plays by the player's lock rules. The machine turns itself
   * only at knife range, so anywhere else the pilot steers for its lock
   * exactly as the player does.
   */
  if (pSelf->byLock != MECHA_LOCK_HELD
      || fDistance > MECHA_CLOSE_QUARTERS) {
    int iSign = mecha_angle_delta(pSelf->iFacing, iBearing) >= 0 ? 1 : -1;

    /* How hard it pushes the stick is part of being good at this: a limp
     * steer keeps losing the enemy off the edge of the cone. [AI-02] */
    pOut->iTurn = iSign * pProfile->iTurnPercent;
    /* Well off the nose, a boost snaps the lock on faster than turning
     * does, and is worth the gauge. */
    if (iOff > MECHA_LOCK_CONE && fBoost > 0.35f && !pOut->bGuard)
      pOut->bDash = true;
  }

  /* --- watching where it puts its feet ---------------------------------- */

  /*
   * The last word on where the machine goes, after the dodging and the lock,
   * because either will happily spend a burst over an edge. No path-finding:
   * do not step off, do not spend a burst that ends off, cancel one that is
   * already going off. [AI-05]
   */
  {
    /* A wall is not a hazard: only an arena you can leave has an edge worth
     * avoiding. A pit is worth avoiding anywhere. [AI-05] */
    bool bEdgeKills = pWorld->arena.byShape == MECHA_ARENA_OPEN;
    bool bCommitted = pSelf->byMove == MECHA_MOVE_DASH
                      || pSelf->iCoastTicks > 0;
    float fForwardX = mecha_sin(pSelf->iFacing);
    float fForwardZ = mecha_cos(pSelf->iFacing);
    float fSpeed = mecha_length2(pSelf->fVelX, pSelf->fVelZ);
    float fMoveX = (float)pOut->iMoveX / 100.0f;
    float fMoveZ = (float)pOut->iMoveZ / 100.0f;
    float fWantX = fForwardX * fMoveZ + fForwardZ * fMoveX;
    float fWantZ = fForwardZ * fMoveZ - fForwardX * fMoveX;
    bool bBack = false;

    /* Pressing boost buys the whole burst, coast and all, in one press. */
    if (pOut->bDash && !bCommitted
        && !mecha_ai_footing_clear(
               pWorld, pSelf, fWantX, fWantZ,
               pDef->fDashSpeed
                   * (float)(pDef->iDashTicks + MECHA_DASH_COAST_TICKS)
                   * MECHA_TICK_SECONDS,
               bEdgeKills)) {
      pOut->bDash = false;
      bBack = true;
    }

    /* On foot, a stride of reaction and whatever it is still carrying. */
    if (!bCommitted
        && !mecha_ai_footing_clear(pWorld, pSelf, fWantX, fWantZ,
                                   mecha_ai_stopping_look(
                                       pDef, fSpeed, MECHA_AI_FOOTING_LEAD),
                                   bEdgeKills)) {
      pOut->bDash = false;
      bBack = true;
    }

    if (bBack) {
      pOut->iMoveX = -pOut->iMoveX;
      pOut->iMoveZ = -pOut->iMoveZ;
      bFooting = true;
    }

    /* Letting go of the stick is not stopping, so what the machine is still
     * carrying gets checked whether it asked for it or not. [AI-05] */
    if (!bCommitted && pSelf->byMove != MECHA_MOVE_JUMP
        && pSelf->byMove != MECHA_MOVE_CANCEL
        && !mecha_ai_footing_clear(pWorld, pSelf, pSelf->fVelX, pSelf->fVelZ,
                                   mecha_ai_stopping_look(
                                       pDef, fSpeed, MECHA_AI_FOOTING_LEAD),
                                   bEdgeKills)) {
      float fOutX = -pSelf->fVelX / fSpeed;
      float fOutZ = -pSelf->fVelZ / fSpeed;

      (void)mecha_ai_footing_escape(pWorld, pSelf, pSelf->fVelX, pSelf->fVelZ,
                                    mecha_ai_stopping_look(
                                        pDef, fSpeed, MECHA_AI_FOOTING_LEAD),
                                    bEdgeKills, &fOutX, &fOutZ);
      pOut->iMoveZ = (int)((fOutX * fForwardX + fOutZ * fForwardZ) * 100.0f);
      pOut->iMoveX = (int)((fOutX * fForwardZ - fOutZ * fForwardX) * 100.0f);
      pOut->bDash = false;
      pOut->bGuard = false;
      bFooting = true;
    }

    /* Airborne there is nothing to brake against, so it leans towards the
     * middle of the arena rather than pretending it can stop. [AI-05] */
    if (pSelf->byMove == MECHA_MOVE_JUMP
        || pSelf->byMove == MECHA_MOVE_CANCEL) {
      float fLook = mecha_ai_stopping_look(pDef, fSpeed,
                                           MECHA_AI_FOOTING_AIR);
      float fOutX;
      float fOutZ;

      if (!mecha_ai_footing_clear(pWorld, pSelf, pSelf->fVelX, pSelf->fVelZ,
                                  fLook, bEdgeKills)
          && mecha_ai_footing_escape(pWorld, pSelf, pSelf->fVelX,
                                     pSelf->fVelZ, fLook, bEdgeKills, &fOutX,
                                     &fOutZ)) {
        pOut->iMoveZ = (int)((fOutX * fForwardX + fOutZ * fForwardZ)
                             * 100.0f);
        pOut->iMoveX = (int)((fOutX * fForwardZ - fOutZ * fForwardX)
                             * 100.0f);
        pOut->bGuard = false;
        bFooting = true;
      }
    }

    /* A burst in flight is committed, so the way out is the player's: push
     * back against it and boost again. [AI-05] */
    if (bCommitted) {
      float fCarryX = pSelf->byMove == MECHA_MOVE_DASH ? pSelf->fDashDirX
                                                       : pSelf->fVelX;
      float fCarryZ = pSelf->byMove == MECHA_MOVE_DASH ? pSelf->fDashDirZ
                                                       : pSelf->fVelZ;
      float fCarry = mecha_length2(fCarryX, fCarryZ);

      if (fCarry > 0.01f
          && !mecha_ai_footing_clear(pWorld, pSelf, fCarryX, fCarryZ,
                                     mecha_ai_stopping_look(
                                       pDef, fSpeed,
                                       MECHA_AI_FOOTING_CANCEL),
                                     bEdgeKills)) {
        /* Back against the line it is travelling, in its own terms, and
         * the boost button to make the cancel take. */
        float fOutX = -fCarryX / fCarry;
        float fOutZ = -fCarryZ / fCarry;

        (void)mecha_ai_footing_escape(pWorld, pSelf, fCarryX, fCarryZ,
                                      mecha_ai_stopping_look(
                                        pDef, fSpeed,
                                        MECHA_AI_FOOTING_CANCEL),
                                      bEdgeKills, &fOutX, &fOutZ);
        pOut->iMoveZ = (int)((fOutX * fForwardX + fOutZ * fForwardZ)
                             * 100.0f);
        pOut->iMoveX = (int)((fOutX * fForwardZ - fOutZ * fForwardX)
                             * 100.0f);
        /* The press, not the holding: a cancel needs a fresh one. Let the
         * button up, then press again. [AI-06] */
        pOut->bDash = !pSelf->bDashHeld;
        pOut->bGuard = false;
        bFooting = true;
      }
    }
  }

  /* --- shooting --------------------------------------------------------- */

shooting:

  /*
   * Held fire is applied at the trigger, so a pilot with it set still closes,
   * circles and dodges. A pilot saving its footing does not shoot at all:
   * firing locks it out of acting, and it cannot steer while it cannot act.
   * [AI-07]
   */
  iSlot = (pWorld->bAiHoldFire || bFooting)
              ? -1
              : mecha_ai_choose_weapon(pWorld, iMechIdx, fDistance, bHasLine);
  if (iSlot >= 0 && pSelf->iRecovery <= 0 && iOff <= MECHA_AI_FIRE_CONE
      /* No lock, no lead. Firing anyway just empties the magazine into the
       * space beside them. */
      && pSelf->byLock == MECHA_LOCK_HELD
      && !pSelf->abFireHeld[iSlot]
      /* A moment's hesitation on the shot rather than the trigger coming
       * down the instant the solution is good. */
      && mecha_rng_range(&pWorld->rng, pProfile->iTriggerOdds) == 0) {
    /*
     * How badly this pilot is about to shoot. Rolled once per shot on the
     * shared RNG, and read by mecha_sim_fire after it has worked out the
     * lead. Every weapon aims itself, so this is the only source of misses.
     * [AI-02]
     */
    pWorld->aMechs[iMechIdx].iAimError =
        pProfile->iAimError > 0
          ? mecha_rng_range(&pWorld->rng, 2 * pProfile->iAimError + 1)
            - pProfile->iAimError
          : 0;

    switch (iSlot) {
    case MECHA_SLOT_LEFT:   pOut->bFireLeft = true; break;
    case MECHA_SLOT_CENTER: pOut->bFireCenter = true; break;
    default:                pOut->bFireRight = true; break;
    }
  }
}
