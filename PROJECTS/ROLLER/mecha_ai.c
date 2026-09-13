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

/* How far that way the machine gets before there is nothing to stand on,
 * sampling the whole way rather than the far end of it: the far lip of a gap
 * is ground too. Returns fLook when the way is clear. [AI-12] */
static float mecha_ai_footing_run(const tMechaWorld *pWorld,
                                  const tMechaMech *pSelf, float fDirX,
                                  float fDirZ, float fLook, bool bEdgeKills)
{
  float fLen = mecha_length2(fDirX, fDirZ);
  int iSteps;
  int i;

  if (fLen <= 0.01f || fLook <= 0.0f)
    return fLook;

  iSteps = (int)(fLook / MECHA_AI_FOOTING_STEP) + 1;

  for (i = 1; i <= iSteps; i++) {
    float fAt = fLook * (float)i / (float)iSteps;
    float fX = pSelf->fX + fDirX / fLen * fAt;
    float fZ = pSelf->fZ + fDirZ / fLen * fAt;
    bool bClear;

    /*
     * A drop is a drop however the arena makes one. A pit is a flag and the
     * edge of an open arena is its boundary, but ground that simply falls
     * away -- the sides of a causeway, the far side of a roof -- is
     * neither, and a pilot that only knew about the other two walked off
     * it. [AI-11]
     */
    bClear = (mecha_arena_surface(&pWorld->arena, fX, fZ) & MECHA_SURF_PIT)
                 == 0
             && mecha_arena_ground_height(&pWorld->arena, fX, fZ, pSelf->fY)
                  >= pSelf->fGroundY - MECHA_AI_FOOTING_DROP
             && (!bEdgeKills || mecha_arena_contains(&pWorld->arena, fX, fZ));

    if (!bClear)
      return fLook * (float)(i - 1) / (float)iSteps;
  }

  return fLook;
}

//-------------------------------------------------------------------------------------------------

/* Is there still an arena fLook metres that way? The direction need not be
 * normalised; a zero-length one is going nowhere and so is always fine. */
static bool mecha_ai_footing_clear(const tMechaWorld *pWorld,
                                   const tMechaMech *pSelf, float fDirX,
                                   float fDirZ, float fLook, bool bEdgeKills)
{
  return mecha_ai_footing_run(pWorld, pSelf, fDirX, fDirZ, fLook, bEdgeKills)
         >= fLook - 0.01f;
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

/*
 * The heading the stick is read against, which is the machine's own except
 * while it is recentring -- mecha_stick_direction picks the same one, and a
 * pilot that asked for a world direction against the wrong frame would get
 * a different one. [SIM-21]
 */
static int mecha_ai_stick_ref(const tMechaMech *pSelf)
{
  return pSelf->iRecentreTicks > 0 ? pSelf->iStickYaw : pSelf->iFacing;
}

/* A world direction as the two components of the stick that asks for it. */
static void mecha_ai_stick_for(const tMechaMech *pSelf, float fDirX,
                               float fDirZ, int *piMoveX, int *piMoveZ)
{
  int iRef = mecha_ai_stick_ref(pSelf);
  float fForwardX = mecha_sin(iRef);
  float fForwardZ = mecha_cos(iRef);
  float fLen = mecha_length2(fDirX, fDirZ);

  if (fLen <= 0.01f) {
    *piMoveX = 0;
    *piMoveZ = 0;
    return;
  }
  fDirX /= fLen;
  fDirZ /= fLen;
  *piMoveZ = (int)((fDirX * fForwardX + fDirZ * fForwardZ) * 100.0f);
  *piMoveX = (int)((fDirX * fForwardZ - fDirZ * fForwardX) * 100.0f);
}

//-------------------------------------------------------------------------------------------------

/*
 * Is there something solid between here and a stride that way, at this much
 * of the machine's own height? [AI-09]
 */
static bool mecha_ai_way_blocked(const tMechaWorld *pWorld, int iMechIdx,
                                 float fDirX, float fDirZ, float fLook,
                                 float fRise)
{
  const tMechaMech *pSelf = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_def_get((int)pSelf->byDefIdx);
  float fLen = mecha_length2(fDirX, fDirZ);
  float fEye;

  if (fLen <= 0.01f)
    return false;

  fEye = pSelf->fY + pDef->fHeight * fRise;
  return mecha_arena_trace_segment(&pWorld->arena,
                                   pSelf->fX, fEye, pSelf->fZ,
                                   pSelf->fX + fDirX / fLen * fLook, fEye,
                                   pSelf->fZ + fDirZ / fLen * fLook,
                                   NULL, NULL, NULL);
}

//-------------------------------------------------------------------------------------------------

/*
 * A wall rather than a step: something that blocks the machine's body and
 * is still there a jump higher up.
 *
 * The ground query cannot answer this. It hides any box whose roof is more
 * than a step above the feet -- which is exactly the tall ones -- so a
 * building reads through it as flat ground and a pilot that asked it would
 * walk into the side of a tower block believing it could step over it.
 * Two traces at two heights is what actually distinguishes a crate from a
 * building. [AI-09]
 */
static bool mecha_ai_wall_ahead(const tMechaWorld *pWorld, int iMechIdx,
                                float fDirX, float fDirZ, float fLook)
{
  if (!mecha_ai_way_blocked(pWorld, iMechIdx, fDirX, fDirZ, fLook, 0.55f))
    return false;
  return mecha_ai_way_blocked(pWorld, iMechIdx, fDirX, fDirZ, fLook,
                              MECHA_AI_DETOUR_WALL);
}

//-------------------------------------------------------------------------------------------------

/*
 * A way past it. Fans out either side of where the pilot wanted to go and
 * takes the first bearing that is not blocked, nearest side first, so a
 * machine rounds cover the short way rather than turning its back on the
 * fight. False when it is walled in every way it looked, and the caller
 * carries on as it was rather than standing still. [AI-09]
 */
static bool mecha_ai_detour(const tMechaWorld *pWorld, int iMechIdx,
                            float fLook, float *pfDirX, float *pfDirZ)
{
  float fLen = mecha_length2(*pfDirX, *pfDirZ);
  int iFan;

  if (fLen <= 0.01f)
    return false;
  /* A crate is not in the way: the pilot jumps onto those. */
  if (!mecha_ai_wall_ahead(pWorld, iMechIdx, *pfDirX, *pfDirZ, fLook))
    return false;

  for (iFan = 1; iFan <= MECHA_AI_DETOUR_FANS; iFan++) {
    int iSide;

    for (iSide = 0; iSide < 2; iSide++) {
      int iTurn = iSide ? -iFan * MECHA_AI_DETOUR_STEP
                        : iFan * MECHA_AI_DETOUR_STEP;
      float fCos = mecha_cos(iTurn);
      float fSin = mecha_sin(iTurn);
      float fTryX = (*pfDirX * fCos - *pfDirZ * fSin) / fLen;
      float fTryZ = (*pfDirX * fSin + *pfDirZ * fCos) / fLen;

      if (mecha_ai_way_blocked(pWorld, iMechIdx, fTryX, fTryZ, fLook,
                               0.55f))
        continue;
      *pfDirX = fTryX;
      *pfDirZ = fTryZ;
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

/*
 * A way round a gap: the bearing off the wanted one that gets furthest
 * before the ground runs out. Not the first that is clear, the way the
 * detour past a building works [AI-09] -- every bearing over a hole is clear
 * somewhere past the far lip. Ties go to the smaller turn and then to the
 * side the machine is already drifting. Overwrites the direction. [AI-12]
 */
static bool mecha_ai_skirt(const tMechaWorld *pWorld, const tMechaMech *pSelf,
                           float fLook, bool bEdgeKills, float *pfDirX,
                           float *pfDirZ)
{
  float fLen = mecha_length2(*pfDirX, *pfDirZ);
  float fWantX;
  float fWantZ;
  float fBest;
  float fBestX = 0.0f;
  float fBestZ = 0.0f;
  int iDrift;
  int iFan;

  if (fLen <= 0.01f)
    return false;
  fWantX = *pfDirX / fLen;
  fWantZ = *pfDirZ / fLen;

  /* Which way it is already going, in the same terms as the fan: the side
   * the velocity leans is the side tried first. */
  iDrift = (pSelf->fVelX * fWantZ - pSelf->fVelZ * fWantX) >= 0.0f ? 1 : -1;

  fBest = mecha_ai_footing_run(pWorld, pSelf, fWantX, fWantZ, fLook,
                               bEdgeKills);

  for (iFan = 1; iFan <= MECHA_AI_SKIRT_FANS; iFan++) {
    int iSide;

    for (iSide = 0; iSide < 2; iSide++) {
      int iTurn = iFan * MECHA_AI_SKIRT_STEP * (iSide ? -iDrift : iDrift);
      float fCos = mecha_cos(iTurn);
      float fSin = mecha_sin(iTurn);
      float fTryX = fWantX * fCos - fWantZ * fSin;
      float fTryZ = fWantX * fSin + fWantZ * fCos;
      float fRun = mecha_ai_footing_run(pWorld, pSelf, fTryX, fTryZ, fLook,
                                        bEdgeKills);

      if (fRun <= fBest)
        continue;
      fBest = fRun;
      fBestX = fTryX;
      fBestZ = fTryZ;
    }
  }

  if (fBestX == 0.0f && fBestZ == 0.0f)
    return false;
  *pfDirX = fBestX;
  *pfDirZ = fBestZ;
  return true;
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
  /* Set when the pilot has found a way past whatever is between it and the
   * enemy, which is also a reason not to give up and guard. [AI-09] */
  bool bDetour = false;
  /* Set when the pilot is steering for a point on one of the arena's own
   * ways rather than at the enemy. [AI-13] */
  bool bWay = false;
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
  /* The way round a gap goes stale on its own, whether or not the pilot
   * needs one this tick. [AI-12] */
  if (pWorld->aMechs[iMechIdx].iSkirtTicks > 0)
    pWorld->aMechs[iMechIdx].iSkirtTicks--;
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
    int iDriveOff;
    float fSpeed = mecha_length2(pSelf->fVelX, pSelf->fVelZ);
    bool bClear = true;
    /* Set once the driver is steering for a point on one of the arena's own
     * ways rather than straight at the enemy. [AI-13] */
    bool bOnWay = false;
    float fWayX = pTarget->fX - pSelf->fX;
    float fWayZ = pTarget->fZ - pSelf->fZ;
    float fWayLook = MECHA_AI_WAY_LOOK + fSpeed * MECHA_AI_WAY_LEAD;
    float fAimX;
    float fAimZ;

    /* The race game's own driver, near enough: when the ground straight at
     * the enemy runs out, aim along the arena's way instead. A car needs it
     * most -- it cannot step sideways off a lane. [AI-13] */
    if (!mecha_ai_footing_clear(pWorld, pSelf, fWayX, fWayZ, fWayLook,
                                pWorld->arena.byShape == MECHA_ARENA_OPEN)
        && mecha_arena_way_aim(&pWorld->arena, pSelf->fX, pSelf->fZ,
                               pTarget->fX, pTarget->fZ, fWayLook,
                               (int)pSelf->byAiLine, &fAimX, &fAimZ)) {
      fWayX = fAimX - pSelf->fX;
      fWayZ = fAimZ - pSelf->fZ;
      iDriveBearing = mecha_atan2_angle(fWayX, fWayZ);
      bOnWay = true;
    }

    /*
     * Steer for the enemy, but not through the building in front of the
     * enemy. A car has no reverse worth the name and cannot step sideways,
     * so driving into cover is a car parked there for the rest of the
     * round -- looking one stopping distance ahead and aiming past whatever
     * is there is the whole of its path-finding. [AI-09]
     */
    if (mecha_ai_detour(pWorld, iMechIdx,
                        MECHA_AI_DETOUR_LOOK
                          + fSpeed * MECHA_AI_DETOUR_LEAD,
                        &fWayX, &fWayZ))
      iDriveBearing = mecha_atan2_angle(fWayX, fWayZ);
    iDriveOff = mecha_angle_delta(pSelf->iFacing, iDriveBearing);

    pOut->iTurn = iDriveOff >= 0 ? pProfile->iTurnPercent
                                 : -pProfile->iTurnPercent;
    /* Nearly straight at them: stop sawing at the wheel, or the gun never
     * settles long enough to be worth firing. */
    if (iDriveOff < MECHA_AI_DRIVE_STRAIGHT
        && iDriveOff > -MECHA_AI_DRIVE_STRAIGHT)
      pOut->iTurn = 0;

    /*
     * A car cannot step back, so all it can do about an edge is lift off and
     * turn, watching its own momentum as far ahead as it takes to stop. The
     * exception is the moment of joining a way -- nose on it, way clear,
     * wheels still carrying the turn -- without which no car ever commits to
     * a lane. [AI-13]
     */
    if (fSpeed > 0.01f) {
      bool bEdge = pWorld->arena.byShape == MECHA_ARENA_OPEN;
      float fStop = mecha_ai_stopping_look(pDef, fSpeed,
                                           MECHA_AI_FOOTING_CANCEL);

      bClear = mecha_ai_footing_clear(pWorld, pSelf, pSelf->fVelX,
                                      pSelf->fVelZ, fStop, bEdge);
      if (!bClear && bOnWay && iDriveOff < MECHA_AI_DRIVE_LIFT
          && iDriveOff > -MECHA_AI_DRIVE_LIFT)
        bClear = mecha_ai_footing_clear(pWorld, pSelf, fWayX, fWayZ, fStop,
                                        bEdge);
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

  /*
   * Forward by default. The band it will sit in is narrow and its middle is
   * inside its preferred range rather than on it, so a pilot with nothing
   * else to do closes rather than orbits: holding station at exactly the
   * range the weapon likes reads as hiding, and a pilot that never arrives
   * never makes the other one move. [AI-08]
   */
  if (fDistance > fPreferred * 1.05f) {
    pOut->iMoveZ = 100;
    pOut->iMoveX /= 2;
    /* Closing a long gap is what the boost is for. */
    if (fDistance > fPreferred * 1.5f && fBoost > 0.35f)
      pOut->bDash = true;
  } else if (fDistance < fPreferred * 0.40f) {
    pOut->iMoveZ = -100;
    pOut->iMoveX /= 2;
    if (fBoost > 0.6f)
      pOut->bDash = true;
  } else {
    pOut->iMoveZ = 45;
  }

  /*
   * --- getting round what is in the way ---------------------------------
   *
   * The climb above answers a crate. A building is not a crate: the roof is
   * out of reach, the pilot has no line through it, and pressing forward
   * into it is what left these standing against the side of a tower block
   * for a whole round. So when the way to the enemy is walled, the pilot
   * aims past the wall instead, and spends gauge on getting round it --
   * which is the one thing that turns cover from a place to be stuck into
   * a way to arrive somewhere unexpected. [AI-09]
   */
  if (!bHasLine) {
    float fWayX = pTarget->fX - pSelf->fX;
    float fWayZ = pTarget->fZ - pSelf->fZ;
    float fSpeed = mecha_length2(pSelf->fVelX, pSelf->fVelZ);

    bDetour = mecha_ai_detour(pWorld, iMechIdx,
                              MECHA_AI_DETOUR_LOOK
                                + fSpeed * MECHA_AI_DETOUR_LEAD,
                              &fWayX, &fWayZ);
    if (bDetour) {
      /* The detour is a heading in the world; the stick asks for it in the
       * machine's own frame. */
      mecha_ai_stick_for(pSelf, fWayX, fWayZ, &pOut->iMoveX, &pOut->iMoveZ);
      if (fBoost > 0.3f)
        pOut->bDash = true;
    }
  }

  /*
   * Out of everything, or genuinely spent behind cover: sit down and refill.
   * The boost floor is deliberately low -- a pilot that guards whenever it
   * is merely short of gauge spends most of a fight crouched behind a box,
   * which is what made these look like they were hiding. [AI-08]
   */
  if ((!bAmmoLeft || (!bHasLine && fBoost < 0.15f))
      && !bDetour
      && fDistance > fPreferred * 0.8f) {
    pOut->bGuard = true;
    pOut->iMoveX = 0;
    pOut->iMoveZ = 0;
    pOut->bDash = false;
  }

  /*
   * Take the high ground when there is some, rather than now and then.
   *
   * A box or a building answers the ground query at its roof height, so
   * something to stand on is simply ground ahead that is well above the
   * ground here and not so far above that the jump cannot reach it. Looking
   * along the line to the target means the thing it climbs is the thing
   * between them, which is the one worth being on top of. [AI-08]
   */
  if (!pOut->bGuard && !pDef->bWheeled && fBoost > 0.45f
      && pSelf->byMove != MECHA_MOVE_JUMP
      && pSelf->byMove != MECHA_MOVE_CANCEL) {
    float fLook = pDef->fRadius + MECHA_AI_CLIMB_LOOK;
    float fToX = pTarget->fX - pSelf->fX;
    float fToZ = pTarget->fZ - pSelf->fZ;
    float fLen = mecha_length2(fToX, fToZ);

    if (fLen > 0.01f) {
      float fAheadX = pSelf->fX + fToX / fLen * fLook;
      float fAheadZ = pSelf->fZ + fToZ / fLen * fLook;
      float fStep = mecha_arena_ground_height(&pWorld->arena, fAheadX,
                                              fAheadZ, pSelf->fY)
                    - pSelf->fGroundY;

      if (fStep > MECHA_AI_CLIMB_MIN && fStep < pDef->fHeight * 1.6f)
        pOut->bJump = true;
    }
  }
  if (!bHasLine && fBoost > 0.4f && fDistance < fPreferred
      && mecha_rng_range(&pWorld->rng, 60) == 0)
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

  /* --- following a way across ------------------------------------------- */

  /*
   * A Whiplash driver follows its AI line the whole way round a track; a
   * pilot here follows one of the arena's ways only while the straight line
   * to the enemy has nothing to walk on -- never, on an arena that publishes
   * no way. Aiming at a point along it is the whole of it. [AI-13]
   */
  if (!pOut->bGuard && !pDef->bWheeled) {
    bool bEdge = pWorld->arena.byShape == MECHA_ARENA_OPEN;
    float fSpeed = mecha_length2(pSelf->fVelX, pSelf->fVelZ);
    float fLook = MECHA_AI_WAY_LOOK + fSpeed * MECHA_AI_WAY_LEAD;
    float fToX = pTarget->fX - pSelf->fX;
    float fToZ = pTarget->fZ - pSelf->fZ;
    float fAimX;
    float fAimZ;

    if (!mecha_ai_footing_clear(pWorld, pSelf, fToX, fToZ, fLook, bEdge)
        && mecha_arena_way_aim(&pWorld->arena, pSelf->fX, pSelf->fZ,
                               pTarget->fX, pTarget->fZ, fLook,
                               (int)pSelf->byAiLine, &fAimX, &fAimZ)) {
      float fAimToX = fAimX - pSelf->fX;
      float fAimToZ = fAimZ - pSelf->fZ;

      mecha_ai_stick_for(pSelf, fAimToX, fAimToZ, &pOut->iMoveX,
                         &pOut->iMoveZ);
      /* A way is ground the arena vouches for; getting onto one from where
       * the machine stands is not. So it is walked only as far as the feet
       * can see, and the rest is left to the footing rules below -- which
       * now have the way to aim off instead of the enemy. */
      bWay = mecha_ai_footing_clear(pWorld, pSelf, fAimToX, fAimToZ,
                                    mecha_ai_stopping_look(
                                      pDef, fSpeed, MECHA_AI_FOOTING_LEAD),
                                    bEdge);
    }
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
    float fBurst = pDef->fDashSpeed
                   * (float)(pDef->iDashTicks + MECHA_DASH_COAST_TICKS)
                   * MECHA_TICK_SECONDS;
    float fStride = mecha_ai_stopping_look(pDef, fSpeed,
                                           MECHA_AI_FOOTING_LEAD);
    bool bBack = false;

    /* A burst that would end over nothing is simply not taken. The feet are
     * still where they were, so there is nothing to back away from. [AI-12] */
    if (pOut->bDash && !bCommitted
        && !mecha_ai_footing_clear(pWorld, pSelf, fWantX, fWantZ, fBurst,
                                   bEdgeKills))
      pOut->bDash = false;

    /*
     * On foot, a stride of reaction and whatever it is still carrying. This
     * one is about the ground under the next step, so when it fails the
     * pilot looks for a way round before it gives up and backs off: turning
     * off the line until there is ground again is what walks a causeway,
     * and it is the same move as going round a building [AI-09].
     */
    if (!bCommitted
        && !mecha_ai_footing_clear(pWorld, pSelf, fWantX, fWantZ, fStride,
                                   bEdgeKills)) {
      tMechaMech *pMe = &pWorld->aMechs[iMechIdx];
      float fHeldX = mecha_sin(pMe->iSkirtYaw);
      float fHeldZ = mecha_cos(pMe->iSkirtYaw);

      pOut->bDash = false;
      /* The way round it settled on a moment ago, while that still has
       * ground under it: picking again every tick on a narrow causeway is
       * how a machine walks on the spot. [AI-12] */
      if (pMe->iSkirtTicks > 0
          && mecha_ai_footing_clear(pWorld, pSelf, fHeldX, fHeldZ, fStride,
                                    bEdgeKills)) {
        fWantX = fHeldX;
        fWantZ = fHeldZ;
      } else if (mecha_ai_skirt(pWorld, pSelf, fStride, bEdgeKills, &fWantX,
                                &fWantZ)) {
        pMe->iSkirtYaw = mecha_atan2_angle(fWantX, fWantZ);
        pMe->iSkirtTicks = MECHA_AI_SKIRT_HOLD;
      } else {
        pMe->iSkirtTicks = 0;
        bBack = true;
      }

      /* Walked, not boosted: a burst cannot be taken back, and this heading
       * is only good for the stride that was looked at. */
      if (!bBack)
        mecha_ai_stick_for(pSelf, fWantX, fWantZ, &pOut->iMoveX,
                           &pOut->iMoveZ);
    }

    if (bBack) {
      pOut->iMoveX = -pOut->iMoveX;
      pOut->iMoveZ = -pOut->iMoveZ;
      bFooting = true;
    }

    /* Letting go of the stick is not stopping, so what the machine is still
     * carrying gets checked whether it asked for it or not. [AI-05] */
    /* What it is already carrying, which the stick cannot take back. Not
     * while it walks a way: that ground is the arena's own, and second-
     * guessing it reversed pilots out of causeways. [AI-13] */
    if (!bCommitted && !bWay && fSpeed > MECHA_AI_CARRY_MIN
        && pSelf->byMove != MECHA_MOVE_JUMP
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

  /*
   * --- the crossing step ------------------------------------------------
   *
   * A burst already under way can be turned: let the stick go for a tick and
   * push a new direction, and the machine starts the burst again the new way
   * rather than limping out the old one. [SIM-08]
   *
   * That is the whole of watari-dashing, and it is what lets a pilot leave
   * cover on one heading and arrive on another -- round the side of a
   * building and cut back at the enemy without ever stopping. The pilot
   * takes it whenever what it wants now is far enough off what it launched
   * with to be worth the turn, which after the detour above is exactly when
   * it has cleared the corner. Last, because everything above it may still
   * change its mind about where it is going. [AI-10]
   */
  if (!bFooting && pSelf->byMove == MECHA_MOVE_DASH
      && (pOut->iMoveX != 0 || pOut->iMoveZ != 0)) {
    int iRef = mecha_ai_stick_ref(pSelf);
    float fForwardX = mecha_sin(iRef);
    float fForwardZ = mecha_cos(iRef);
    float fMoveX = (float)pOut->iMoveX / 100.0f;
    float fMoveZ = (float)pOut->iMoveZ / 100.0f;
    float fWantX = fForwardX * fMoveZ + fForwardZ * fMoveX;
    float fWantZ = fForwardZ * fMoveZ - fForwardX * fMoveX;
    float fLen = mecha_length2(fWantX, fWantZ);

    if (fLen > 0.01f) {
      float fDot = (fWantX * pSelf->fDashDirX + fWantZ * pSelf->fDashDirZ)
                   / fLen;

      if (fDot < MECHA_AI_WATARI_DOT) {
        if (!pSelf->bDashStickFree) {
          /* The machine is waiting to see the stick let go, so let it go. */
          pOut->iMoveX = 0;
          pOut->iMoveZ = 0;
        } else {
          /* And now the tap, hard enough to count as one. */
          mecha_ai_stick_for(pSelf, fWantX, fWantZ, &pOut->iMoveX,
                             &pOut->iMoveZ);
        }
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
