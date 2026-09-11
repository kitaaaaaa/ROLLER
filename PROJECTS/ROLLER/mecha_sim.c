#include "mecha_sim.h"

#include "mecha_ai.h"
#include "mecha_arena.h"
#include "mecha_defs.h"

#include <math.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------

#define MECHA_DT           MECHA_TICK_SECONDS
#define MECHA_GROUND_EPS   (0.05f * MECHA_METRE)
#define MECHA_READY_TICKS  MECHA_SEC(2.0f)
#define MECHA_ROUND_OVER_TICKS MECHA_SEC(3.2f)
#define MECHA_MINE_ARM_TICKS 24

/* A knockdown-grade hit that does not floor the mech still interrupts it. */
#define MECHA_STAGGER_INTERRUPT 30.0f

//-------------------------------------------------------------------------------------------------

static const tMechaMechDef *mecha_mech_def(const tMechaMech *pMech)
{
  return mecha_def_get((int)pMech->byDefIdx);
}

//-------------------------------------------------------------------------------------------------

bool mecha_mech_alive(const tMechaMech *pMech)
{
  return pMech && pMech->bActive && pMech->byMove != MECHA_MOVE_DESTROYED
      && pMech->fArmour > 0.0f;
}

//-------------------------------------------------------------------------------------------------

eMechaStance mecha_mech_stance(const tMechaMech *pMech)
{
  if (!pMech)
    return MECHA_STANCE_STAND;

  switch (pMech->byMove) {
  case MECHA_MOVE_GUARD:  return MECHA_STANCE_GUARD;
  case MECHA_MOVE_DASH:   return MECHA_STANCE_DASH;
  case MECHA_MOVE_JUMP:
  case MECHA_MOVE_CANCEL: return MECHA_STANCE_JUMP;
  default:                return MECHA_STANCE_STAND;
  }
}

//-------------------------------------------------------------------------------------------------

/* Stunned, floored, getting up, or still recovering from a landing: no
 * movement input and no trigger is accepted. */
static bool mecha_can_act(const tMechaMech *pMech)
{
  if (!mecha_mech_alive(pMech))
    return false;
  if (pMech->iStunTicks > 0)
    return false;
  switch (pMech->byMove) {
  case MECHA_MOVE_STAGGER:
  case MECHA_MOVE_DOWN:
  case MECHA_MOVE_RISE:
  case MECHA_MOVE_LAND:
  /* The drop is committed once it starts, the same as the landing it ends
   * in. Cancelling is a decision, not a free reposition. */
  case MECHA_MOVE_CANCEL:
    return false;
  default:
    return true;
  }
}

//-------------------------------------------------------------------------------------------------

const tMechaWeaponDef *mecha_mech_weapon(const tMechaWorld *pWorld,
                                         int iMechIdx, int iSlot)
{
  const tMechaMech *pMech;

  if (!pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return NULL;
  if (iSlot < 0 || iSlot >= MECHA_WEAPON_SLOTS)
    return NULL;

  pMech = &pWorld->aMechs[iMechIdx];
  if (!pMech->bActive)
    return NULL;
  return &mecha_mech_def(pMech)->aWeapons[iSlot][mecha_mech_stance(pMech)];
}

//-------------------------------------------------------------------------------------------------

float mecha_mech_boost_fraction(const tMechaWorld *pWorld, int iMechIdx)
{
  const tMechaMech *pMech;
  const tMechaMechDef *pDef;
  int iMax;

  if (!pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return 0.0f;
  pMech = &pWorld->aMechs[iMechIdx];
  if (!pMech->bActive)
    return 0.0f;

  pDef = mecha_mech_def(pMech);
  iMax = pDef->iBoostMax * MECHA_BOOST_SCALE;
  if (iMax <= 0)
    return 0.0f;
  return mecha_clampf((float)pMech->iBoost / (float)iMax, 0.0f, 1.0f);
}

//-------------------------------------------------------------------------------------------------

float mecha_mech_armour_fraction(const tMechaWorld *pWorld, int iMechIdx)
{
  const tMechaMech *pMech;
  const tMechaMechDef *pDef;

  if (!pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return 0.0f;
  pMech = &pWorld->aMechs[iMechIdx];
  if (!pMech->bActive)
    return 0.0f;

  pDef = mecha_mech_def(pMech);
  if (pDef->fArmour <= 0.0f)
    return 0.0f;
  return mecha_clampf(pMech->fArmour / pDef->fArmour, 0.0f, 1.0f);
}

//-------------------------------------------------------------------------------------------------

float mecha_mech_centre_height(const tMechaWorld *pWorld, int iMechIdx)
{
  const tMechaMech *pMech;

  if (!pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return 0.0f;
  pMech = &pWorld->aMechs[iMechIdx];
  if (!pMech->bActive)
    return 0.0f;
  return pMech->fY + mecha_mech_def(pMech)->fHeight * 0.55f;
}

//-------------------------------------------------------------------------------------------------

float mecha_mech_ground_height(const tMechaWorld *pWorld, int iMechIdx)
{
  const tMechaMech *pMech;

  if (!pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return 0.0f;
  pMech = &pWorld->aMechs[iMechIdx];
  if (!pMech->bActive)
    return 0.0f;
  return mecha_arena_ground_height(&pWorld->arena, pMech->fX, pMech->fZ,
                                   pMech->fY);
}

//-------------------------------------------------------------------------------------------------

bool mecha_mech_is_airborne(const tMechaWorld *pWorld, int iMechIdx)
{
  const tMechaMech *pMech;

  if (!pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return false;
  pMech = &pWorld->aMechs[iMechIdx];
  if (!pMech->bActive)
    return false;
  return pMech->fY > mecha_mech_ground_height(pWorld, iMechIdx)
                     + MECHA_GROUND_EPS;
}

//-------------------------------------------------------------------------------------------------

int mecha_sim_human_index(const tMechaWorld *pWorld)
{
  int i;

  if (!pWorld)
    return -1;
  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    if (pWorld->aMechs[i].bActive
        && pWorld->aMechs[i].byController == MECHA_CONTROL_HUMAN)
      return i;
  }
  return -1;
}

//-------------------------------------------------------------------------------------------------

int mecha_sim_nearest_enemy(const tMechaWorld *pWorld, int iMechIdx)
{
  const tMechaMech *pSelf;
  float fBestSq = 0.0f;
  int iBest = -1;
  int i;

  if (!pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return -1;
  pSelf = &pWorld->aMechs[iMechIdx];
  if (!pSelf->bActive)
    return -1;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    const tMechaMech *pOther = &pWorld->aMechs[i];
    float fDx;
    float fDz;
    float fDistSq;

    if (i == iMechIdx || !mecha_mech_alive(pOther))
      continue;
    if (pOther->byTeam == pSelf->byTeam)
      continue;

    fDx = pOther->fX - pSelf->fX;
    fDz = pOther->fZ - pSelf->fZ;
    fDistSq = fDx * fDx + fDz * fDz;
    if (iBest < 0 || fDistSq < fBestSq) {
      fBestSq = fDistSq;
      iBest = i;
    }
  }
  return iBest;
}

//-------------------------------------------------------------------------------------------------

/* Defined with the weapon code further down, needed by the burst above it. */
static void mecha_direction_from_angles(int iYaw, int iPitch,
                                        float *pfX, float *pfY, float *pfZ);

/* First free slot, or NULL when the table is full. */
static tMechaEffect *mecha_alloc_effect(tMechaWorld *pWorld, uint8_t byKind,
                                        float fX, float fY, float fZ,
                                        float fScale, uint8_t byPalette,
                                        int iLife)
{
  int i;

  for (i = 0; i < MECHA_MAX_EFFECTS; i++) {
    tMechaEffect *pFx = &pWorld->aEffects[i];

    if (pFx->bActive)
      continue;
    memset(pFx, 0, sizeof(*pFx));
    pFx->bActive = true;
    pFx->byKind = byKind;
    pFx->byPalette = byPalette;
    pFx->fX = fX;
    pFx->fY = fY;
    pFx->fZ = fZ;
    pFx->fScale = fScale;
    pFx->iLife = iLife;
    return pFx;
  }
  return NULL;
}

//-------------------------------------------------------------------------------------------------

/*
 * A burst of debris thrown out of a point.
 *
 * Directions come off the shared RNG so a replay throws the same sparks the
 * same way. Speed is jittered per particle rather than fixed, because a ring
 * of debris all travelling at one speed reads as a expanding shell, which is
 * the exact thing the single billboard already looked like.
 */
static void mecha_spawn_burst(tMechaWorld *pWorld, float fX, float fY,
                              float fZ, float fSpeed, float fScale,
                              int iCount, int iLife)
{
  int i;

  for (i = 0; i < iCount; i++) {
    tMechaEffect *pFx;
    int iYaw = mecha_rng_range(&pWorld->rng, MECHA_ANGLE_FULL);
    /* Biased upwards: debris that only ever went sideways looked like a
     * puddle spreading. */
    int iPitch = mecha_rng_range(&pWorld->rng, MECHA_ANGLE_QUARTER)
                 - MECHA_ANGLE_QUARTER / 5;
    float fThis = fSpeed * (0.45f + 0.55f * mecha_rng_unit(&pWorld->rng));
    float fDirX;
    float fDirY;
    float fDirZ;

    pFx = mecha_alloc_effect(pWorld, MECHA_FX_EMBER, fX, fY, fZ,
                             fScale * (0.6f + 0.8f * mecha_rng_unit(&pWorld->rng)),
                             0, iLife);
    if (!pFx)
      return;
    mecha_direction_from_angles(iYaw, iPitch, &fDirX, &fDirY, &fDirZ);
    pFx->fVelX = fDirX * fThis;
    pFx->fVelY = fDirY * fThis;
    pFx->fVelZ = fDirZ * fThis;
  }
}

//-------------------------------------------------------------------------------------------------

void mecha_sim_spawn_effect(tMechaWorld *pWorld, uint8_t byKind,
                            float fX, float fY, float fZ,
                            float fScale, uint8_t byPalette, int iLife)
{
  if (!pWorld || iLife <= 0)
    return;

  if (mecha_alloc_effect(pWorld, byKind, fX, fY, fZ, fScale, byPalette,
                         iLife))
    return;
  /* The effect table is cosmetic. When it is full the oldest survivors keep
   * playing and the new puff is simply dropped, which is invisible in
   * practice and keeps the simulation allocation-free. */
}

//-------------------------------------------------------------------------------------------------

/*
 * How much of a hit a guarding mech keeps out.
 *
 * Only melee, and only while actually in the stance. Guard is a posture for
 * answering something that has closed the distance, not a shield -- standing
 * in it against gunfire has to lose, or the fast boost refill it already
 * grants would make it the only thing anyone ever does. Returns 1.0 for
 * every case that is not a guarded melee hit, so callers can multiply
 * unconditionally.
 */
static void mecha_guard_mitigation(const tMechaMech *pVictim, uint8_t byKind,
                                   float *pfDamageScale,
                                   float *pfStaggerScale)
{
  *pfDamageScale = 1.0f;
  *pfStaggerScale = 1.0f;
  if (!pVictim || byKind != MECHA_PROJ_MELEE)
    return;
  if (pVictim->byMove != MECHA_MOVE_GUARD)
    return;
  *pfDamageScale = MECHA_GUARD_MELEE_DAMAGE;
  *pfStaggerScale = MECHA_GUARD_MELEE_STAGGER;
}

//-------------------------------------------------------------------------------------------------

void mecha_sim_damage(tMechaWorld *pWorld, int iVictimIdx, int iAttackerIdx,
                      float fDamage, float fStagger,
                      float fPushX, float fPushZ)
{
  tMechaMech *pVictim;
  const tMechaMechDef *pDef;
  float fMass;

  if (!pWorld || iVictimIdx < 0 || iVictimIdx >= MECHA_MAX_MECHS)
    return;
  pVictim = &pWorld->aMechs[iVictimIdx];
  if (!mecha_mech_alive(pVictim) || pVictim->iInvulnTicks > 0)
    return;

  pDef = mecha_mech_def(pVictim);
  fMass = pDef->fMass > 0.1f ? pDef->fMass : 0.1f;

  pVictim->fArmour -= fDamage;
  if (iAttackerIdx >= 0 && iAttackerIdx < MECHA_MAX_MECHS
      && pWorld->aMechs[iAttackerIdx].bActive)
    pWorld->aMechs[iAttackerIdx].fDamageDealt += fDamage;

  /* Knockback and stagger both scale against mass, so the same shot shoves a
   * light interceptor and barely rocks a siege platform. */
  pVictim->fVelX += fPushX / fMass;
  pVictim->fVelZ += fPushZ / fMass;
  pVictim->fStagger += fStagger / fMass;

  /*
   * And it shudders. The race game shakes a car by road speed, which a
   * walking machine has no equivalent of; what it does have is the moment
   * something hits it, so that is what feeds the same noise here. Against
   * mass, like everything else on this line: the same shell rattles a
   * light machine and barely disturbs a heavy one.
   */
  pVictim->attitude.fHitShake =
    mecha_clampf(pVictim->attitude.fHitShake
                   + fDamage * MECHA_SHAKE_HIT_PER_HP / fMass,
                 0.0f, 1.0f);

  if (pVictim->fArmour <= 0.0f) {
    pVictim->fArmour = 0.0f;
    pVictim->byMove = MECHA_MOVE_DESTROYED;
    pVictim->iStateTicks = 0;
    pVictim->iStunTicks = 0;
    pVictim->fVelX = 0.0f;
    pVictim->fVelZ = 0.0f;
    /* A quarter of the mech's height, because the effect's scale is a
     * billboard half-extent: the blast is that much again on every side, so
     * this already paints a square about half as wide as the machine is
     * tall. Passing a figure near the height itself -- as this did -- puts a
     * flat opaque slab wider than the mech across the middle of the screen
     * on the one frame the player most needs to see what happened. */
    mecha_sim_spawn_effect(pWorld, MECHA_FX_EXPLOSION, pVictim->fX,
                           pVictim->fY + pDef->fHeight * 0.5f, pVictim->fZ,
                           pDef->fHeight * 0.25f, pDef->abyPalette[3],
                           MECHA_SEC(0.9f));
    /* The flash alone was one quad appearing and vanishing. The debris is
     * what makes a kill read as a machine coming apart. */
    mecha_spawn_burst(pWorld, pVictim->fX,
                      pVictim->fY + pDef->fHeight * 0.5f, pVictim->fZ,
                      MECHA_MPS(34.0f), pDef->fRadius * 0.16f, 14,
                      MECHA_SEC(1.5f));
    return;
  }

  if (pVictim->fStagger >= MECHA_STAGGER_DOWN) {
    pVictim->fStagger = 0.0f;
    pVictim->byMove = MECHA_MOVE_DOWN;
    pVictim->iStateTicks = 0;
    pVictim->iStunTicks = MECHA_DOWN_TICKS;
    pVictim->iRecovery = 0;
    pVictim->iLungeTicks = 0;
  } else if (fStagger >= MECHA_STAGGER_INTERRUPT
             && pVictim->byMove != MECHA_MOVE_JUMP) {
    /* Enough to break a firing animation, not enough to floor anyone.
     * Airborne mechs are left alone so a mid-air trade does not turn into a
     * free fall every time. */
    pVictim->byMove = MECHA_MOVE_STAGGER;
    pVictim->iStateTicks = 0;
    pVictim->iStunTicks = MECHA_STAGGER_TICKS;
    pVictim->iRecovery = 0;
    pVictim->iLungeTicks = 0;
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_sim_explode(tMechaWorld *pWorld, int iOwnerIdx,
                              float fX, float fY, float fZ,
                              float fRadius, float fDamage, float fStagger,
                              uint8_t byPalette)
{
  int i;

  /* Half the blast radius, because the effect's scale is a billboard
   * half-extent: passing the radius itself paints a quad twice the width of
   * the blast, which reads as a wall rather than a burst. */
  mecha_sim_spawn_effect(pWorld, MECHA_FX_EXPLOSION, fX, fY, fZ,
                         fRadius * 0.5f, byPalette, MECHA_SEC(0.5f));
  mecha_spawn_burst(pWorld, fX, fY, fZ, MECHA_MPS(22.0f), fRadius * 0.06f,
                    7, MECHA_SEC(0.8f));

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    tMechaMech *pMech = &pWorld->aMechs[i];
    const tMechaMechDef *pDef;
    float fDx;
    float fDy;
    float fDz;
    float fDist;
    float fFalloff;

    if (i == iOwnerIdx || !mecha_mech_alive(pMech))
      continue;

    /* Measured to the mech's middle, so a blast at its feet and one at its
     * head are worth the same. */
    pDef = mecha_mech_def(pMech);
    fDx = pMech->fX - fX;
    fDy = (pMech->fY + pDef->fHeight * 0.5f) - fY;
    fDz = pMech->fZ - fZ;
    fDist = mecha_length3(fDx, fDy, fDz) - pDef->fRadius;
    if (fDist < 0.0f)
      fDist = 0.0f;
    if (fDist >= fRadius)
      continue;

    fFalloff = 1.0f - fDist / fRadius;
    if (fDist > 1e-3f) {
      float fScale = fFalloff * fDamage * 0.02f * MECHA_METRE / fDist;
      mecha_sim_damage(pWorld, i, iOwnerIdx, fDamage * fFalloff,
                       fStagger * fFalloff, fDx * fScale, fDz * fScale);
    } else {
      mecha_sim_damage(pWorld, i, iOwnerIdx, fDamage * fFalloff,
                       fStagger * fFalloff, 0.0f, 0.0f);
    }
  }
}

//-------------------------------------------------------------------------------------------------
/* Targeting */

static void mecha_update_target(tMechaWorld *pWorld, int iMechIdx,
                                bool bCyclePressed)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMech *pTarget;

  if (bCyclePressed) {
    /* Step to the next live enemy after the current one, wrapping. With two
     * mechs in the arena this is a no-op, which is exactly right. */
    int iStart = pMech->iTargetIdx >= 0 ? pMech->iTargetIdx : iMechIdx;
    int i;

    for (i = 1; i <= MECHA_MAX_MECHS; i++) {
      int iCandidate = (iStart + i) % MECHA_MAX_MECHS;
      const tMechaMech *pOther = &pWorld->aMechs[iCandidate];

      if (iCandidate == iMechIdx || !mecha_mech_alive(pOther))
        continue;
      if (pOther->byTeam == pMech->byTeam)
        continue;
      pMech->iTargetIdx = iCandidate;
      break;
    }
  }

  /* A lock that has gone stale -- destroyed, or walked out of range -- falls
   * back to whoever is nearest rather than leaving the reticle stuck. */
  if (pMech->iTargetIdx >= 0 && pMech->iTargetIdx < MECHA_MAX_MECHS) {
    pTarget = &pWorld->aMechs[pMech->iTargetIdx];
    if (mecha_mech_alive(pTarget) && pTarget->byTeam != pMech->byTeam) {
      float fDx = pTarget->fX - pMech->fX;
      float fDz = pTarget->fZ - pMech->fZ;

      if (mecha_length2(fDx, fDz) <= MECHA_LOCK_RANGE)
        return;
    }
  }
  pMech->iTargetIdx = mecha_sim_nearest_enemy(pWorld, iMechIdx);
}

//-------------------------------------------------------------------------------------------------

/*
 * Whether the mech is actually tracking whoever the reticle is on.
 *
 * mecha_update_target picks who; this decides whether the lock is live. It
 * holds while the target sits inside a generous cone of the mech's own
 * heading and drops once it has been outside for the grace period, at which
 * point the auto-turn stops following and every weapon fires straight down
 * the barrel. Boosting or jumping snaps it back on from any angle, which is
 * what makes those worth spending gauge on for reasons other than distance.
 *
 * Reads byMove as movement left it last tick. One tick of lag on a dash that
 * lasts dozens does not matter, and running before movement is what lets the
 * facing update downstream act on a fresh lock.
 */
static void mecha_update_lock(tMechaWorld *pWorld, int iMechIdx)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMech *pTarget;
  int iBearing;
  int iOff;

  if (!mecha_mech_alive(pMech)
      || pMech->iTargetIdx < 0 || pMech->iTargetIdx >= MECHA_MAX_MECHS) {
    pMech->byLock = MECHA_LOCK_NONE;
    pMech->iLockSlipTicks = 0;
    return;
  }
  pTarget = &pWorld->aMechs[pMech->iTargetIdx];
  if (!mecha_mech_alive(pTarget)) {
    pMech->byLock = MECHA_LOCK_NONE;
    pMech->iLockSlipTicks = 0;
    return;
  }

  /* Off the ground or riding a boost, the lock comes on from any angle. */
  if (pMech->byMove == MECHA_MOVE_DASH || pMech->byMove == MECHA_MOVE_JUMP
      || pMech->byMove == MECHA_MOVE_CANCEL) {
    pMech->byLock = MECHA_LOCK_HELD;
    pMech->iLockSlipTicks = 0;
    return;
  }

  iBearing = mecha_atan2_angle(pTarget->fX - pMech->fX,
                               pTarget->fZ - pMech->fZ);
  iOff = mecha_angle_delta(pMech->iFacing, iBearing);
  if (iOff < 0)
    iOff = -iOff;

  if (pMech->byLock == MECHA_LOCK_NONE) {
    /* Broken locks do not drift back on. Line the machine up, or boost. */
    if (iOff <= MECHA_LOCK_REACQUIRE_CONE) {
      pMech->byLock = MECHA_LOCK_HELD;
      pMech->iLockSlipTicks = 0;
    }
    return;
  }

  if (iOff <= MECHA_LOCK_CONE) {
    pMech->byLock = MECHA_LOCK_HELD;
    pMech->iLockSlipTicks = 0;
    return;
  }

  /* The grace period stops a lock dying to one frame of overshoot. */
  pMech->iLockSlipTicks++;
  pMech->byLock = pMech->iLockSlipTicks >= MECHA_LOCK_BREAK_TICKS
                    ? MECHA_LOCK_NONE : MECHA_LOCK_SLIPPING;
}

//-------------------------------------------------------------------------------------------------

/* Bearing and elevation from this mech to its lock. Returns false when there
 * is nothing locked, leaving the outputs untouched. */
static bool mecha_aim_at_target(const tMechaWorld *pWorld, int iMechIdx,
                                int *piBearing, int *piElevation)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMech *pTarget;
  float fDx;
  float fDy;
  float fDz;
  float fFlat;

  if (pMech->iTargetIdx < 0 || pMech->iTargetIdx >= MECHA_MAX_MECHS)
    return false;
  pTarget = &pWorld->aMechs[pMech->iTargetIdx];
  if (!mecha_mech_alive(pTarget))
    return false;

  fDx = pTarget->fX - pMech->fX;
  fDz = pTarget->fZ - pMech->fZ;
  fDy = mecha_mech_centre_height(pWorld, pMech->iTargetIdx)
      - mecha_mech_centre_height(pWorld, iMechIdx);
  fFlat = mecha_length2(fDx, fDz);

  if (piBearing)
    *piBearing = mecha_atan2_angle(fDx, fDz);
  if (piElevation) {
    if (fFlat < 1.0f)
      *piElevation = fDy >= 0.0f ? MECHA_ANGLE_QUARTER : -MECHA_ANGLE_QUARTER;
    else
      *piElevation = mecha_atan2_angle(fDy, fFlat);
  }
  return true;
}

//-------------------------------------------------------------------------------------------------
/* Movement */

static bool mecha_boost_available(const tMechaMech *pMech)
{
  return !pMech->bBoostLocked && pMech->iBoost > 0;
}

//-------------------------------------------------------------------------------------------------

static void mecha_spend_boost(tMechaMech *pMech, int iAmount)
{
  pMech->iBoost -= iAmount;
  if (pMech->iBoost <= 0) {
    pMech->iBoost = 0;
    pMech->bBoostLocked = true;
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_regain_boost(tMechaMech *pMech, int iAmount)
{
  const tMechaMechDef *pDef = mecha_mech_def(pMech);
  int iMax = pDef->iBoostMax * MECHA_BOOST_SCALE;

  pMech->iBoost += iAmount;
  if (pMech->iBoost > iMax)
    pMech->iBoost = iMax;
  if (pMech->bBoostLocked
      && pMech->iBoost >= iMax * MECHA_BOOST_UNLOCK_NUM / MECHA_BOOST_UNLOCK_DEN)
    pMech->bBoostLocked = false;
}

//-------------------------------------------------------------------------------------------------

/* The stick's world-space direction, relative to the mech's own heading.
 * Returns the stick magnitude in 0..1; the direction outputs are only
 * meaningful when that is above zero. */
static float mecha_stick_direction(const tMechaMech *pMech,
                                   const tMechaInput *pInput,
                                   float *pfDirX, float *pfDirZ)
{
  float fX = (float)pInput->iMoveX / 100.0f;
  float fZ = (float)pInput->iMoveZ / 100.0f;
  float fMag = mecha_length2(fX, fZ);
  float fForwardX;
  float fForwardZ;
  float fRightX;
  float fRightZ;

  *pfDirX = 0.0f;
  *pfDirZ = 0.0f;
  if (fMag < 0.08f)
    return 0.0f;
  if (fMag > 1.0f) {
    fX /= fMag;
    fZ /= fMag;
    fMag = 1.0f;
  }

  fForwardX = mecha_sin(pMech->iFacing);
  fForwardZ = mecha_cos(pMech->iFacing);
  fRightX = fForwardZ;
  fRightZ = -fForwardX;

  *pfDirX = fForwardX * fZ + fRightX * fX;
  *pfDirZ = fForwardZ * fZ + fRightZ * fX;
  return fMag;
}

//-------------------------------------------------------------------------------------------------

/* Flat distance to whatever this mech has locked, or a very large number
 * when it has nothing. */
static float mecha_target_range(const tMechaWorld *pWorld, int iMechIdx)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMech *pTarget;

  if (pMech->iTargetIdx < 0 || pMech->iTargetIdx >= MECHA_MAX_MECHS)
    return 1e9f;
  pTarget = &pWorld->aMechs[pMech->iTargetIdx];
  if (!mecha_mech_alive(pTarget))
    return 1e9f;
  return mecha_length2(pTarget->fX - pMech->fX, pTarget->fZ - pMech->fZ);
}

//-------------------------------------------------------------------------------------------------

static void mecha_update_facing(tMechaWorld *pWorld, int iMechIdx,
                                const tMechaInput *pInput, bool bCanAct)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_mech_def(pMech);
  int iMaxStep = (int)(pDef->fTurnRate * MECHA_DT);
  int iBearing;
  int iElevation;
  bool bFreeTurn = pMech->iFreeTurnTicks > 0;

  if (iMaxStep < 1)
    iMaxStep = 1;

  /* The window a jump cancel's landing opens. It lifts the turn rate off
   * both the lock and the sticks, and it works through the landing recovery
   * that otherwise refuses input -- coming down facing the other way is the
   * entire reason to have cancelled. */
  if (bFreeTurn) {
    iMaxStep *= MECHA_CANCEL_TURN_SCALE;
    pMech->iFreeTurnTicks--;
  }

  if (pMech->byLock == MECHA_LOCK_HELD
      && mecha_aim_at_target(pWorld, iMechIdx, &iBearing, &iElevation)) {
    /* Elevation comes off the lock at any range. It tilts the guns rather
     * than the machine, so it costs the player nothing. */
    pMech->iAimPitch = mecha_clampi(iElevation >= MECHA_ANGLE_HALF
                                      ? iElevation - MECHA_ANGLE_FULL
                                      : iElevation,
                                    -MECHA_AIM_PITCH_LIMIT,
                                    MECHA_AIM_PITCH_LIMIT);

    /* The shoulders only follow at knife range, where an exchange is too
     * fast to aim by hand. Further out the machine points where it is
     * pointed -- which is what makes holding a lock at range a thing the
     * player does rather than a thing that happens. */
    if (!pDef->bWheeled
        && (mecha_target_range(pWorld, iMechIdx) <= MECHA_CLOSE_QUARTERS
            || pMech->iRecentreTicks > 0)) {
      /*
       * And never on wheels, at any range. A car points where it is
       * driving; that is the whole of its handling and the whole of its
       * aiming, and an auto-turn would be the machine steering itself.
       * Holding a lock in one means driving at somebody and keeping them
       * in the middle of the screen.
       */
      pMech->iFacing = mecha_angle_approach(pMech->iFacing, iBearing,
                                            iMaxStep);
    }
    if (pMech->iRecentreTicks > 0)
      pMech->iRecentreTicks--;
  } else {
    pMech->iAimPitch = 0;
  }

  /* Manual turn rides on top, for shaking a lock loose or for lining one up
   * again once it has gone. */
  if (pDef->bWheeled) {
    /*
     * Steering, not turning. Whiplash works the lock out as
     * `input * (1 + (top - speed) / k)` and then throws it away entirely
     * below the car's own steering speed limit, and both halves are here:
     * the lock is widest just off a standstill and narrows as the speed
     * comes up, and a car that is not moving cannot be pointed at all.
     *
     * It is the stick as much as the turn axis, because a car has no
     * strafe for the stick to mean anything else by.
     */
    float fSpeed = mecha_length2(pMech->fVelX, pMech->fVelZ);
    float fAlong = pMech->fVelX * mecha_sin(pMech->iFacing)
                   + pMech->fVelZ * mecha_cos(pMech->iFacing);
    int iSteer = pInput->iTurn + pInput->iMoveX;

    if (bCanAct && iSteer != 0 && fSpeed >= pDef->fSteerFloor
        && pDef->fWalkSpeed > 0.0f) {
      float fSlack = 1.0f - mecha_clampf(fSpeed / pDef->fWalkSpeed, 0.0f,
                                         1.0f);
      float fLock = 1.0f + MECHA_CAR_STEER_GAIN * fSlack;
      int iStep = (int)(pDef->fTurnRate * MECHA_DT * fLock
                        * (float)mecha_clampi(iSteer, -100, 100) / 100.0f);

      /*
       * Backwards, the wheels point the other way round -- but only when
       * the car is actually in reverse, not merely sliding.
       *
       * Whiplash decides this on fFinalSpeed, the car's own signed speed
       * along its nose, and on a track that is the only speed it has:
       * position is advanced straight along the heading, so a Whiplash car
       * cannot travel at an angle to where it points. This one carries a
       * real velocity vector, and in a drift that vector swings more than
       * a quarter turn off the nose -- at which point a test on the dot
       * product decides the car is reversing and flips the steering, which
       * stops the slide dead. That was the rotation limit: not a clamp
       * anywhere, but the stick fighting the spin halfway through it.
       *
       * Reverse is slow -- a third of the forward top speed -- and a drift
       * is fast, so the car's own reverse speed separates the two cleanly.
       */
      if (fAlong < 0.0f
          && fSpeed <= pDef->fWalkSpeed * MECHA_CAR_REVERSE)
        iStep = -iStep;
      pMech->iFacing = mecha_angle_wrap(pMech->iFacing + iStep);
    }
  } else if ((bCanAct || bFreeTurn) && pInput->iTurn != 0) {
    int iManual = (int)(pDef->fTurnRate * MECHA_DT
                        * (float)pInput->iTurn / 100.0f);

    if (bFreeTurn)
      iManual *= MECHA_CANCEL_TURN_SCALE;
    pMech->iFacing = mecha_angle_wrap(pMech->iFacing + iManual);
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * Hitting something solid.
 *
 * The push the arena applied to get the machine back out is the surface
 * normal, which is all a bounce needs. At walking pace the machine simply
 * leans on the wall and the speed into it is dropped -- pressing into a
 * corner should not build up a shove that fires you out of it later. Carry a
 * boost into the same wall and it comes off, the way the race game's cars
 * do, and the burst is over: you hit something.
 */
static void mecha_wall_impact(tMechaWorld *pWorld, int iMechIdx,
                              float fPushX, float fPushZ, bool bAirborne)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_mech_def(pMech);
  float fLength = mecha_length2(fPushX, fPushZ);
  float fInto;
  float fSpeed;
  bool bFast;

  if (fLength < 1e-3f)
    return;
  fPushX /= fLength;
  fPushZ /= fLength;

  /* Positive means the machine is already on its way out of the wall, which
   * happens on the tick after a bounce. Nothing to do. */
  fInto = pMech->fVelX * fPushX + pMech->fVelZ * fPushZ;
  if (fInto >= 0.0f)
    return;

  fSpeed = mecha_length2(pMech->fVelX, pMech->fVelZ);
  bFast = (pMech->byMove == MECHA_MOVE_DASH || pMech->iCoastTicks > 0)
          && fSpeed >= MECHA_BOUNCE_MIN_SPEED;

  pMech->fVelX -= (bFast ? 1.0f + MECHA_BOUNCE_RESTITUTION : 1.0f)
                  * fInto * fPushX;
  pMech->fVelZ -= (bFast ? 1.0f + MECHA_BOUNCE_RESTITUTION : 1.0f)
                  * fInto * fPushZ;
  if (!bFast)
    return;

  if (pMech->byMove == MECHA_MOVE_DASH) {
    pMech->byMove = bAirborne ? MECHA_MOVE_JUMP : MECHA_MOVE_STAND;
    pMech->iStateTicks = 0;
  }
  /* Off the wall with the clock reset, so the ricochet carries as far as the
   * burst that caused it would have. */
  pMech->iCoastTicks = MECHA_DASH_COAST_TICKS;
  mecha_sim_spawn_effect(pWorld, MECHA_FX_SPARK,
                         pMech->fX + fPushX * pDef->fRadius,
                         pMech->fY + pDef->fHeight * 0.45f,
                         pMech->fZ + fPushZ * pDef->fRadius,
                         pDef->fRadius * 0.4f, pDef->abyPalette[3],
                         MECHA_SEC(0.2f));
}

//-------------------------------------------------------------------------------------------------

static void mecha_start_dash(tMechaMech *pMech, const tMechaInput *pInput)
{
  float fDirX;
  float fDirZ;

  if (mecha_stick_direction(pMech, pInput, &fDirX, &fDirZ) > 0.0f) {
    pMech->fDashDirX = fDirX;
    pMech->fDashDirZ = fDirZ;
  } else {
    /* No stick means dash straight ahead. */
    pMech->fDashDirX = mecha_sin(pMech->iFacing);
    pMech->fDashDirZ = mecha_cos(pMech->iFacing);
  }
  pMech->byMove = MECHA_MOVE_DASH;
  pMech->iStateTicks = 0;
  pMech->iCoastTicks = 0;
  /* Whatever the stick was doing when the burst started does not count as a
   * steering input: it has to be let go first. */
  pMech->bDashStickFree = false;
}

//-------------------------------------------------------------------------------------------------

/*
 * Steering a burst you are already committed to.
 *
 * Two ways in. Boost again while pushing back against the direction you left
 * on and the dash restarts the other way -- the cancel, and the reason a
 * committed dash is not a trap. Or let the stick go and tap a new direction:
 * the burst turns without a second press, which is the crossing step, and
 * the release is the whole cost of it.
 */
static void mecha_steer_dash(tMechaMech *pMech, const tMechaInput *pInput,
                             float fStick, float fDirX, float fDirZ,
                             bool bDashPressed)
{
  float fDot;

  if (fStick < MECHA_DASH_STICK_FREE) {
    pMech->bDashStickFree = true;
    return;
  }

  fDot = fDirX * pMech->fDashDirX + fDirZ * pMech->fDashDirZ;
  if (bDashPressed && fDot < MECHA_DASH_CANCEL_DOT) {
    mecha_start_dash(pMech, pInput);
    return;
  }
  if (pMech->bDashStickFree && fStick > MECHA_DASH_STICK_TAP) {
    pMech->fDashDirX = fDirX;
    pMech->fDashDirZ = fDirZ;
    /*
     * And the burst starts again the new way rather than limping out the
     * remainder of the old one. A crossing step is a dash that changed its
     * mind, not the tail of one -- what stops it going on forever is the
     * gauge, which is still draining the whole time.
     */
    pMech->iStateTicks = 0;
    /* One turn per release, so leaning on the stick does not steer the
     * burst round in a circle. */
    pMech->bDashStickFree = false;
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * Drives the machine towards a velocity instead of assigning one.
 *
 * The velocity it already has is split into the part pointing where it is
 * being asked to go and the part pointing across that. The first is pushed
 * towards the speed asked for at the machine's own drive rate; the second is
 * bled off at its grip. That single split is what makes a heavy machine
 * slide out of a direction change and a light one snap round -- the sideways
 * component is the skid, and grip is how fast it stops being one.
 *
 * fDirX/fDirZ must be unit length, or zero to mean "no direction asked for",
 * in which case everything is treated as sideways and simply brakes.
 */
static void mecha_drive(tMechaMech *pMech, const tMechaMechDef *pDef,
                        float fDirX, float fDirZ, float fSpeed,
                        float fAccelScale, float fGripScale)
{
  float fGrip = pDef->fGrip > 0.0f ? pDef->fGrip : MECHA_MPS(90.0f);
  float fAccel = pDef->fDriveAccel > 0.0f ? pDef->fDriveAccel
                                                : MECHA_MPS(70.0f);
  float fAlong;
  float fPerpX;
  float fPerpZ;

  fGrip *= fGripScale * MECHA_DT;
  fAccel *= fAccelScale * MECHA_DT;

  if (fDirX == 0.0f && fDirZ == 0.0f) {
    pMech->fVelX = mecha_approachf(pMech->fVelX, 0.0f, fGrip);
    pMech->fVelZ = mecha_approachf(pMech->fVelZ, 0.0f, fGrip);
    return;
  }

  fAlong = pMech->fVelX * fDirX + pMech->fVelZ * fDirZ;
  fPerpX = pMech->fVelX - fDirX * fAlong;
  fPerpZ = pMech->fVelZ - fDirZ * fAlong;

  fAlong = mecha_approachf(fAlong, fSpeed, fAccel);
  fPerpX = mecha_approachf(fPerpX, 0.0f, fGrip);
  fPerpZ = mecha_approachf(fPerpZ, 0.0f, fGrip);

  pMech->fVelX = fDirX * fAlong + fPerpX;
  pMech->fVelZ = fDirZ * fAlong + fPerpZ;
}

/*
 * The states nobody drives: floored, getting up, reeling from a hit, or
 * still absorbing a landing. Each runs on its own clock and none of them
 * takes input, so both the legged machines and the wheeled one hand them
 * the same few ticks of bookkeeping before deciding anything else. Returns
 * true when the machine is in one of them and the caller should keep its
 * hands off.
 */
static bool mecha_advance_recovery(tMechaMech *pMech,
                                   const tMechaMechDef *pDef, bool bAirborne)
{
  if (!mecha_mech_alive(pMech)) {
    pMech->fVelX = 0.0f;
    pMech->fVelZ = 0.0f;
    return true;
  }
  if (pMech->byMove == MECHA_MOVE_DOWN) {
    if (pMech->iStunTicks <= 0) {
      pMech->byMove = MECHA_MOVE_RISE;
      pMech->iStateTicks = 0;
      pMech->iStunTicks = MECHA_RISE_TICKS;
      pMech->iInvulnTicks = MECHA_RISE_INVULN;
    }
    return true;
  }
  if (pMech->byMove == MECHA_MOVE_RISE) {
    if (pMech->iStunTicks <= 0) {
      pMech->byMove = MECHA_MOVE_STAND;
      pMech->iStateTicks = 0;
    }
    return true;
  }
  if (pMech->byMove == MECHA_MOVE_STAGGER) {
    if (pMech->iStunTicks <= 0) {
      pMech->byMove = bAirborne ? MECHA_MOVE_JUMP : MECHA_MOVE_STAND;
      pMech->iStateTicks = 0;
    }
    return true;
  }
  if (pMech->byMove == MECHA_MOVE_LAND) {
    if (pMech->iStateTicks >= pDef->iLandTicks) {
      pMech->byMove = MECHA_MOVE_STAND;
      pMech->iStateTicks = 0;
    }
    return true;
  }
  return false;
}

//-------------------------------------------------------------------------------------------------

/*
 * Which pedal is down. Boost is the accelerator and guard is the brake,
 * which is what the two buttons are for on a machine with no gauge to spend
 * and nothing to guard with. The stick answers as well, because a car
 * nobody can drive with the same keys they walk everything else with is a
 * car nobody drives.
 */
static int mecha_car_throttle(const tMechaInput *pInput, bool bCanAct)
{
  if (!bCanAct)
    return 0;
  if (pInput->bDash || pInput->iMoveZ > 40)
    return 1;
  if (pInput->bGuard || pInput->iMoveZ < -40)
    return -1;
  return 0;
}

//-------------------------------------------------------------------------------------------------

/*
 * Driving.
 *
 * A wheeled machine has one number for its motion and it points along the
 * nose: throttle and brakes move it, the tyres kill anything sideways, and
 * the steering turns the nose rather than the machine. There is no strafe
 * because a car has none, no boost because it does not need one, and no
 * jump because it has no legs to jump with -- but gravity still applies, so
 * driving off a roof does exactly what driving off a roof does.
 *
 * The steering is the race game's, in shape and in both of its rules: the
 * lock is widest just off a standstill and narrows as the speed comes up,
 * and below the machine's own steering floor there is no steering at all.
 * That second rule is why this thing has to keep moving to point at
 * anybody, which is the whole of how it fights.
 */
static void mecha_update_wheels(tMechaWorld *pWorld, int iMechIdx,
                                const tMechaInput *pInput, bool bCanAct)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_mech_def(pMech);
  float fGround = mecha_arena_ground_height(&pWorld->arena, pMech->fX,
                                            pMech->fZ, pMech->fY);
  bool bAirborne = pMech->fY > fGround + MECHA_GROUND_EPS;
  float fNoseX = mecha_sin(pMech->iFacing);
  float fNoseZ = mecha_cos(pMech->iFacing);
  float fAlong = pMech->fVelX * fNoseX + pMech->fVelZ * fNoseZ;
  float fTop = pDef->fWalkSpeed;
  float fTarget = 0.0f;
  float fScale = 1.0f;
  int iThrottle = 0;

  pMech->iStateTicks++;
  if (mecha_advance_recovery(pMech, pDef, bAirborne))
    return;

  iThrottle = mecha_car_throttle(pInput, bCanAct);

  if (iThrottle > 0) {
    pMech->byMove = MECHA_MOVE_DASH;
    fTarget = fTop;
  } else if (iThrottle < 0) {
    /* Brakes first, reverse afterwards: standing on it while rolling
     * forwards stops the car, and only once it has stopped does it back
     * up, at a fraction of the speed it goes forwards. */
    pMech->byMove = MECHA_MOVE_GUARD;
    fTarget = fAlong > 0.0f ? 0.0f : -fTop * MECHA_CAR_REVERSE;
    fScale = pDef->fDriveAccel > 0.0f ? pDef->fBrake / pDef->fDriveAccel
                                      : 1.0f;
  } else {
    /* Freewheeling: it slows, but nothing like as fast as it stops. */
    pMech->byMove = fAlong * fAlong > MECHA_CAR_ROLLING * MECHA_CAR_ROLLING
                      ? MECHA_MOVE_WALK : MECHA_MOVE_STAND;
    fTarget = 0.0f;
    fScale = MECHA_CAR_DRAG;
  }

  /*
   * In the air a car is a thrown object: the wheels have nothing to push
   * against and nothing to grip with, so it keeps what it had.
   */
  if (bAirborne)
    pMech->byMove = MECHA_MOVE_JUMP;
  else
    mecha_drive(pMech, pDef, fNoseX, fNoseZ, fTarget, fScale, 1.0f);

  pMech->iCoastTicks = 0;
}

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------

/*
 * One axis of body shake: white noise, redrawn every tick.
 *
 * Whiplash writes this as (ROLLERrand() - 0x4000) * work / iStabilityFactor,
 * where the rand is a full 15-bit draw, so the noise is symmetric about
 * level and unfiltered -- a fresh number every frame rather than anything
 * that wanders. That is what makes it read as vibration and not as sway.
 */
static int mecha_shake_axis(tMechaRng *pRng, float fWork)
{
  return (int)((mecha_rng_unit(pRng) * 2.0f - 1.0f) * MECHA_SHAKE_GAIN
               * fWork);
}

//-------------------------------------------------------------------------------------------------

/*
 * Whiplash's decay factors are per tick at 36 Hz. Applying one of them
 * sixty times a second instead of thirty-six would damp the wobble out
 * nearly twice as fast, so each is raised to the ratio of the two rates --
 * which is what makes a second of ringing here a second of ringing there.
 */
static float mecha_whip_decay(float fPerTick36)
{
  return powf(fPerTick36, MECHA_WHIP_HZ / (float)MECHA_TICK_HZ);
}

//-------------------------------------------------------------------------------------------------

/*
 * How the body sits.
 *
 * Everything here is drawn and nothing here is simulated: not one of these
 * angles is read back by the movement code, by collision, or by the firing
 * solution. That is deliberate and it is also what makes the whole thing
 * affordable -- a machine can be squatting, ringing and rattling at once
 * because none of the three has to agree with the others about anything.
 */
static void mecha_update_attitude(tMechaWorld *pWorld, int iMechIdx,
                                  const tMechaInput *pInput, bool bCanAct)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_mech_def(pMech);
  tMechaAttitude *pAtt = &pMech->attitude;
  float fGround = mecha_arena_ground_height(&pWorld->arena, pMech->fX,
                                            pMech->fZ, pMech->fY);
  bool bAirborne = pMech->fY > fGround + MECHA_GROUND_EPS;
  float fSpeed = mecha_length2(pMech->fVelX, pMech->fVelZ);
  float fTop = pDef->fWalkSpeed > 1.0f ? pDef->fWalkSpeed : 1.0f;

  /* --- the tilt that answers the stick ----------------------------------
   *
   * Both games do this and they do it in opposite directions, which is the
   * only interesting thing about it. A car leans out of the corner because
   * that is what weight transfer does to a body on springs; a robot leans
   * into it because a machine that has already started moving before it
   * has moved feels quicker to the hands than one that has not. Neither is
   * more than a couple of degrees.
   */
  {
    int iSign = pDef->bWheeled ? MECHA_TILT_CAR_SIGN : MECHA_TILT_MECH_SIGN;
    int iCeiling = pDef->bWheeled ? MECHA_TILT_LIMIT : MECHA_TILT_MECH_LIMIT;
    int iSteer = 0;

    if (bCanAct && !bAirborne && mecha_mech_alive(pMech)) {
      if (pDef->bWheeled) {
        /*
         * The wheels do nothing below the steering floor, so neither does
         * the body: a car rolling at a walking pace does not load a
         * spring. Whiplash zeroes its own steering input the same way, and
         * for the same reason, before it ever reaches the roll.
         */
        if (fSpeed >= pDef->fSteerFloor)
          iSteer = pInput->iTurn + pInput->iMoveX;
      } else {
        /*
         * Input, not travel. There is already a lean that follows the
         * velocity, and it is not this: this one is on the stick, so it
         * arrives before the machine does.
         */
        iSteer = pInput->iMoveX;
      }
      iSteer = mecha_clampi(iSteer, -100, 100);
    }

    if (iSteer != 0) {
      /* Whiplash's steering is a digital left or right and its tilt has one
       * size to match. A stick that can be half over should get half the
       * lean, so the limit is scaled and the winding rate is not: full
       * deflection then behaves exactly as the original does. */
      int iLimit = iCeiling * (iSteer < 0 ? -iSteer : iSteer) / 100;
      int iWant = iSteer > 0 ? iSign * iLimit : -iSign * iLimit;

      pAtt->iRollSteer = mecha_stepi(pAtt->iRollSteer, iWant,
                                          MECHA_TILT_RATE);
    } else {
      pAtt->iRollSteer = mecha_stepi(pAtt->iRollSteer, 0,
                                          MECHA_TILT_CENTRE);
    }
  }

  /* --- squat and dive, which only a car has ----------------------------- */
  if (pDef->bWheeled && !bAirborne) {
    int iThrottle = mecha_mech_alive(pMech)
                      ? mecha_car_throttle(pInput, bCanAct) : 0;

    if (iThrottle > 0)
      pAtt->iPitchDrive = mecha_clampi(pAtt->iPitchDrive + MECHA_SQUAT_RATE,
                                       -MECHA_SQUAT_LIMIT,
                                       MECHA_SQUAT_LIMIT);
    else if (iThrottle < 0)
      pAtt->iPitchDrive = mecha_clampi(pAtt->iPitchDrive - MECHA_SQUAT_RATE,
                                       -MECHA_SQUAT_LIMIT,
                                       MECHA_SQUAT_LIMIT);
    else
      pAtt->iPitchDrive = mecha_stepi(pAtt->iPitchDrive, 0,
                                           MECHA_SQUAT_RECOVER);
  } else {
    pAtt->iPitchDrive = mecha_stepi(pAtt->iPitchDrive, 0,
                                         MECHA_SQUAT_RECOVER);
  }

  /* --- the nose in the air ----------------------------------------------
   *
   * A car that has left the road points where it is going rather than
   * where it was pointed, which Whiplash gets from the arctangent of the
   * climb against the run. Walkers are excluded: a mech in the air is
   * jumping, and a jumping mech that pitches nose-down on the way back
   * looks like a mech that has been shot.
   */
  if (pDef->bWheeled && bAirborne) {
    pAtt->iAirPitch = mecha_angle_wrap(mecha_atan2_angle(-pMech->fVelY,
                                                         fSpeed));
    if (pAtt->iAirPitch > MECHA_ANGLE_HALF)
      pAtt->iAirPitch -= MECHA_ANGLE_FULL;
    pAtt->iAirPitch = mecha_clampi(pAtt->iAirPitch, -MECHA_AIR_PITCH_LIMIT,
                                   MECHA_AIR_PITCH_LIMIT);
  } else {
    /*
     * Back on the ground it goes straight to level, and does not unwind:
     * the landing has already copied it into the wobble, which is what
     * carries the attitude from here. Leaving it to decay would have the
     * two of them describing the same motion at once.
     */
    pAtt->iAirPitch = 0;
  }

  /* --- what is left of the last landing --------------------------------- */
  {
    float fAmp = pAtt->fWobblePitchAmp < 0.0f ? -pAtt->fWobblePitchAmp
                                              : pAtt->fWobblePitchAmp;
    float fRollAmp = pAtt->fWobbleRollAmp < 0.0f ? -pAtt->fWobbleRollAmp
                                                 : pAtt->fWobbleRollAmp;

    if (fAmp < MECHA_WOBBLE_FLOOR && fRollAmp < MECHA_WOBBLE_FLOOR) {
      pAtt->fWobblePitchAmp = 0.0f;
      pAtt->fWobbleRollAmp = 0.0f;
      pAtt->iWobblePhase = 0;
      pAtt->iPitchWobble = 0;
      pAtt->iRollWobble = 0;
    } else {
      /*
       * The larger the wobble the faster it dies, which is why the two
       * decay rates are named max and min the way round they are: 0.95 is
       * the "max" and it is the smaller number. Blended by amplitude
       * measured in quarter-circles, exactly as the original blends it.
       */
      float fBlend = fAmp * (MECHA_WOBBLE_DECAY_MAX - MECHA_WOBBLE_DECAY_MIN)
                     * MECHA_WOBBLE_BLEND + MECHA_WOBBLE_DECAY_MIN;
      float fCos;

      pAtt->fWobblePitchAmp *= mecha_whip_decay(fBlend);
      pAtt->fWobbleRollAmp *= mecha_whip_decay(MECHA_WOBBLE_ROLL_DECAY);
      pAtt->iWobblePhase = (pAtt->iWobblePhase + 1) % MECHA_ANGLE_FULL;
      fCos = mecha_cos(mecha_angle_wrap(MECHA_WOBBLE_FREQ
                                        * pAtt->iWobblePhase));
      pAtt->iPitchWobble = (int)(pAtt->fWobblePitchAmp * fCos);
      pAtt->iRollWobble = (int)(pAtt->fWobbleRollAmp * fCos);
    }
  }

  /* --- the shake --------------------------------------------------------- */
  {
    float fHealth = pDef->fArmour > 0.0f
                      ? mecha_clampf(pMech->fArmour / pDef->fArmour, 0.0f,
                                     1.0f)
                      : 1.0f;
    float fHurt = 1.0f + (MECHA_SHAKE_DAMAGE_MAX - 1.0f) * (1.0f - fHealth);
    float fWork;

    if (pDef->bWheeled) {
      /*
       * Road speed times damage, which is the race game's own product and
       * the reason it works: a healthy car at speed barely blurs, a
       * wrecked one at speed shakes itself apart, and a wreck standing
       * still sits perfectly quiet.
       */
      fWork = (fSpeed / fTop) * fHurt;
    } else {
      /*
       * A walker has nothing equivalent to road speed, so what shakes it
       * is being hit. The impulse is set where the damage lands and bled
       * off here, which puts the shudder on the blow rather than on the
       * walking.
       */
      pAtt->fHitShake = mecha_approachf(pAtt->fHitShake, 0.0f,
                                        MECHA_SHAKE_HIT_DECAY * MECHA_DT);
      fWork = pAtt->fHitShake * fHurt;
    }
    if (!mecha_mech_alive(pMech))
      fWork = 0.0f;
    pAtt->iPitchShake = mecha_shake_axis(&pAtt->shake, fWork);
    pAtt->iRollShake = mecha_shake_axis(&pAtt->shake, fWork);
    pAtt->iYawShake = mecha_shake_axis(&pAtt->shake, fWork);
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_update_movement(tMechaWorld *pWorld, int iMechIdx,
                                  const tMechaInput *pInput, bool bCanAct)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_mech_def(pMech);
  float fGround = mecha_arena_ground_height(&pWorld->arena, pMech->fX,
                                            pMech->fZ, pMech->fY);
  bool bAirborne = pMech->fY > fGround + MECHA_GROUND_EPS;
  bool bBoosting = false;
  float fDirX = 0.0f;
  float fDirZ = 0.0f;
  float fStick = bCanAct ? mecha_stick_direction(pMech, pInput, &fDirX, &fDirZ)
                         : 0.0f;

  if (pDef->bWheeled) {
    /*
     * Everything from here to the integration below is legs: states a
     * walker moves between, a stick that can push it sideways, a gauge it
     * spends. A wheeled machine has none of it and gets its own few lines
     * instead -- but it shares every line after that, because falling,
     * hitting a wall, standing on the ground and going off the edge of the
     * world are the same problems whatever a machine runs on.
     */
    mecha_update_wheels(pWorld, iMechIdx, pInput, bCanAct);
    goto integrate;
  }

  pMech->iStateTicks++;

  /* --- state selection ------------------------------------------------- */

  if (mecha_advance_recovery(pMech, pDef, bAirborne)) {
    /* Floored, getting up, reeling or landing: none of those are steered,
     * and every one of them runs on its own clock. */
  } else if (bAirborne) {
    if (pMech->byMove == MECHA_MOVE_JUMP && bCanAct
        && pInput->bGuard && !pMech->bGuardHeld) {
      /* The cancel. Guard in the air throws the rest of the arc away and
       * drops the mech; the landing is what pays for it. */
      pMech->byMove = MECHA_MOVE_CANCEL;
      pMech->iStateTicks = 0;
      /* The whole point of dropping out of the air is to come down facing
       * them again, so the drop brings the machine round on its own. */
      pMech->iRecentreTicks = MECHA_RECENTRE_TICKS;
    } else if (pMech->byMove == MECHA_MOVE_DASH
               && pMech->iStateTicks < pDef->iDashTicks
               && mecha_boost_available(pMech)) {
      /* An air dash runs the same clock as one on the ground, and can be
       * steered and cancelled the same way. */
      mecha_steer_dash(pMech, pInput, fStick, fDirX, fDirZ,
                       bCanAct && pInput->bDash && !pMech->bDashHeld);
    } else if (bCanAct && pMech->byMove != MECHA_MOVE_CANCEL
               && pInput->bDash && !pMech->bDashHeld
               && mecha_boost_available(pMech)) {
      /* Dashing in the air: a flat burst that holds its height, which is
       * what makes an arc something the other player has to read rather
       * than something they can simply wait out. */
      mecha_start_dash(pMech, pInput);
      pMech->fVelY = 0.0f;
    } else if (pMech->byMove != MECHA_MOVE_JUMP
               && pMech->byMove != MECHA_MOVE_CANCEL
               && pMech->byMove != MECHA_MOVE_DASH) {
      /* Walked off a ledge. */
      pMech->byMove = MECHA_MOVE_JUMP;
      pMech->iStateTicks = 0;
    }
  } else if (bCanAct) {
    bool bJumpPressed = pInput->bJump && !pMech->bJumpHeld;
    bool bDashPressed = pInput->bDash && !pMech->bDashHeld;

    if (bJumpPressed && mecha_boost_available(pMech)) {
      mecha_spend_boost(pMech, pDef->iBoostJumpCost * MECHA_BOOST_SCALE);
      pMech->fVelY = pDef->fJumpVelocity;
      pMech->byMove = MECHA_MOVE_JUMP;
      pMech->iStateTicks = 0;
      bAirborne = true;
      mecha_sim_spawn_effect(pWorld, MECHA_FX_DUST, pMech->fX, fGround,
                             pMech->fZ, pDef->fRadius * 2.0f,
                             pDef->abyPalette[2], MECHA_SEC(0.4f));
    } else if (pMech->byMove == MECHA_MOVE_DASH
               && pMech->iStateTicks < pDef->iDashTicks
               && mecha_boost_available(pMech)) {
      /*
       * Committed. The button starts the burst and does not hold it up:
       * once it is running, only the clock, an empty gauge, a jump or a
       * wall ends it. What the stick can still do is steer it.
       */
      mecha_steer_dash(pMech, pInput, fStick, fDirX, fDirZ, bDashPressed);
    } else if (bDashPressed && mecha_boost_available(pMech)) {
      mecha_start_dash(pMech, pInput);
    } else if (pInput->bGuard) {
      pMech->byMove = MECHA_MOVE_GUARD;
    } else if (fStick > 0.0f) {
      pMech->byMove = MECHA_MOVE_WALK;
    } else {
      pMech->byMove = MECHA_MOVE_STAND;
    }
  } else if (pMech->byMove == MECHA_MOVE_DASH) {
    pMech->byMove = MECHA_MOVE_STAND;
  }

  /* --- velocity -------------------------------------------------------- */

  if (pMech->iLungeTicks > 0) {
    /* A melee swing carries the mech with it; the lunge is the attack. */
    pMech->iLungeTicks--;
    pMech->fVelX = pMech->fDashDirX * pMech->fLungeSpeed;
    pMech->fVelZ = pMech->fDashDirZ * pMech->fLungeSpeed;
  } else if (!mecha_mech_alive(pMech) || pMech->iStunTicks > 0) {
    /* Knocked about: whatever the hit imparted bleeds off on its own. */
    pMech->fVelX = mecha_approachf(pMech->fVelX, 0.0f,
                                   pDef->fWalkSpeed * 2.0f * MECHA_DT);
    pMech->fVelZ = mecha_approachf(pMech->fVelZ, 0.0f,
                                   pDef->fWalkSpeed * 2.0f * MECHA_DT);
  } else if (pMech->byMove == MECHA_MOVE_DASH) {
    pMech->fVelX = pMech->fDashDirX * pDef->fDashSpeed;
    pMech->fVelZ = pMech->fDashDirZ * pDef->fDashSpeed;
    mecha_spend_boost(pMech, pDef->iBoostDashDrain);
    if (pMech->iStateTicks % 4 == 0)
      mecha_sim_spawn_effect(pWorld, MECHA_FX_THRUSTER,
                             pMech->fX, pMech->fY + pDef->fHeight * 0.45f,
                             pMech->fZ, pDef->fRadius,
                             pDef->abyPalette[3], MECHA_SEC(0.25f));
  } else if (pMech->byMove == MECHA_MOVE_JUMP) {
    float fTargetX = fDirX * pDef->fAirSpeed;
    float fTargetZ = fDirZ * pDef->fAirSpeed;
    float fControl = pDef->fAirSpeed * 2.5f * MECHA_DT;

    if (fStick <= 0.0f) {
      fTargetX = pMech->fVelX;
      fTargetZ = pMech->fVelZ;
    }
    pMech->fVelX = mecha_approachf(pMech->fVelX, fTargetX, fControl);
    pMech->fVelZ = mecha_approachf(pMech->fVelZ, fTargetZ, fControl);

    /* Holding jump keeps the thrusters lit, trading gauge for hang time. */
    if (bCanAct && pInput->bJump && mecha_boost_available(pMech)
        && pMech->fVelY > -pDef->fJumpVelocity * 0.35f) {
      mecha_spend_boost(pMech, pDef->iBoostJumpDrain);
      bBoosting = true;
    }
  } else if (pMech->byMove == MECHA_MOVE_CANCEL) {
    /* Straight down, with the carried speed killed off fast. The drop is
     * meant to put the mech on the ground where it already is, not to be a
     * dive that covers distance. */
    pMech->fVelX = mecha_approachf(pMech->fVelX, 0.0f,
                                   pDef->fAirSpeed * 6.0f * MECHA_DT);
    pMech->fVelZ = mecha_approachf(pMech->fVelZ, 0.0f,
                                   pDef->fAirSpeed * 6.0f * MECHA_DT);
    pMech->fVelY = -MECHA_CANCEL_FALL_SPEED;
  } else if (pMech->byMove == MECHA_MOVE_GUARD) {
    pMech->fVelX = 0.0f;
    pMech->fVelZ = 0.0f;
  } else if (pMech->iCoastTicks > 0
             && (pMech->byMove == MECHA_MOVE_WALK
                 || pMech->byMove == MECHA_MOVE_STAND)) {
    /*
     * Out the far side of a burst. The same drive the walk uses, with the
     * authority turned down at both ends: the machine bleeds off the speed
     * it was carrying slowly and slides while it does it, so a boost ends
     * where it was going rather than where the stick is pointing.
     */
    mecha_drive(pMech, pDef, fDirX, fDirZ, pDef->fWalkSpeed * fStick,
                MECHA_COAST_ACCEL_SCALE, MECHA_COAST_GRIP_SCALE);
  } else if (pMech->byMove == MECHA_MOVE_WALK) {
    mecha_drive(pMech, pDef, fDirX, fDirZ, pDef->fWalkSpeed * fStick,
                1.0f, 1.0f);
  } else {
    /* Nothing asked for: everything on the clock is skid, and the machine
     * leans on its brakes rather than its grip. */
    float fGrip = pDef->fGrip > 0.0f ? pDef->fGrip : MECHA_MPS(90.0f);
    float fBrake = pDef->fBrake > 0.0f ? pDef->fBrake : MECHA_MPS(60.0f);

    mecha_drive(pMech, pDef, 0.0f, 0.0f, 0.0f, 1.0f, fBrake / fGrip);
  }

  /* Ending a dash: the burst decides when, not the player. The speed it
   * built is not thrown away with it -- that is what the coast is. */
  if (pMech->byMove == MECHA_MOVE_DASH
      && (pMech->iStateTicks >= pDef->iDashTicks
          || !mecha_boost_available(pMech))) {
    pMech->byMove = bAirborne ? MECHA_MOVE_JUMP : MECHA_MOVE_STAND;
    pMech->iStateTicks = 0;
    pMech->iCoastTicks = MECHA_DASH_COAST_TICKS;
  }

  /* --- boost recovery --------------------------------------------------- */

  if (pMech->byMove != MECHA_MOVE_DASH && !bBoosting) {
    if (pMech->byMove == MECHA_MOVE_GUARD)
      mecha_regain_boost(pMech, pDef->iBoostGuardRegen);
    else if (pMech->byMove != MECHA_MOVE_JUMP)
      mecha_regain_boost(pMech, pDef->iBoostRegen);
  }

  /* --- integration ------------------------------------------------------ */

integrate:
  if (pMech->byMove == MECHA_MOVE_DASH && !pDef->bWheeled
      && pMech->fVelY <= 0.0f) {
    /*
     * A burst is flat, in the air as much as on the ground: gravity waits
     * until it is over. Only while the machine is level or sinking, mind --
     * a boost that has just been thrown off the top of a slope is carrying
     * real upward speed, and holding that would turn a ramp into a ceiling.
     * Rising, it arcs like anything else.
     */
    pMech->fVelY = 0.0f;
  } else if (bAirborne || pMech->byMove == MECHA_MOVE_JUMP
             || pMech->byMove == MECHA_MOVE_CANCEL) {
    float fGravity = MECHA_GRAVITY;

    if (bBoosting)
      fGravity *= 0.18f;
    pMech->fVelY -= fGravity * MECHA_DT;
  } else if (pMech->fVelY <= 0.0f) {
    /*
     * Standing on something, so nothing to fall. Upward speed is left
     * alone: a machine that has just come off the lip of a ramp is still
     * in contact on the tick it happens, and this is where the launch
     * would otherwise be thrown away for the second time.
     */
    pMech->fVelY = 0.0f;
  }

  pMech->fX += pMech->fVelX * MECHA_DT;
  pMech->fZ += pMech->fVelZ * MECHA_DT;
  pMech->fY += pMech->fVelY * MECHA_DT;

  {
    float fPreX = pMech->fX;
    float fPreZ = pMech->fZ;

    if (mecha_arena_resolve_cylinder(&pWorld->arena, pDef->fRadius, pMech->fY,
                                     pDef->fHeight, &pMech->fX, &pMech->fZ)) {
      mecha_wall_impact(pWorld, iMechIdx, pMech->fX - fPreX,
                        pMech->fZ - fPreZ, bAirborne);
    }
  }

  fGround = mecha_arena_ground_height(&pWorld->arena, pMech->fX, pMech->fZ,
                                      pMech->fY);
  if (pMech->fY <= fGround) {
    bool bWasFalling = pMech->fVelY < 0.0f;
    /* How hard it arrived. Taken now because the contact rules below are
     * about to zero the vertical speed, and the landing wobble is sized
     * from the drop. */
    float fImpactVelY = pMech->fVelY;
    /*
     * Off a ramp, the way the race game does it.
     *
     * A car in Whiplash is held to the road by the surface being magnetic;
     * where it is not, the game compares where the car's own momentum would
     * put it against the height of the ground under it, and if the ground
     * has dropped away, the car is in the air. The same rule stated from
     * the other end: on a surface that does not hold you, the rate the
     * ground rose under you this tick is a real upward velocity, and when
     * the slope runs out you keep it.
     *
     * So a machine that walks up a hill is glued to it -- the climb is
     * slow, and the threshold sees to that -- and one that boosts up the
     * same hill leaves the ground at the top.
     */
    float fClimb = (fGround - pMech->fGroundY) / MECHA_DT;
    uint32_t uiSurface = mecha_arena_surface(&pWorld->arena, pMech->fX,
                                             pMech->fZ);

    pMech->fY = fGround;
    if ((uiSurface & MECHA_SURF_NON_MAGNETIC) == 0) {
      pMech->fVelY = 0.0f;                  /* held down, as a track is */
    } else if (fClimb > MECHA_RAMP_LAUNCH_CLIMB) {
      pMech->fVelY = fClimb;                /* the slope is pushing it up */
    } else if (pMech->fVelY <= 0.0f) {
      pMech->fVelY = 0.0f;                  /* landing, or level ground */
    }
    /*
     * The fourth case is the one that matters and it does nothing at all:
     * still in contact, the ground no longer rising, and carrying upward
     * speed from the slope it has just come off. Zeroing that -- which is
     * what the first version of this did -- throws the launch away on the
     * exact tick it should happen, at the lip, and the machine walks onto
     * the flat top as though the ramp had been a staircase.
     */
    if (fImpactVelY < -MECHA_WOBBLE_MIN_DROP
        && pMech->attitude.iAirPitch != 0) {
      /*
       * The landing wobble, seeded the way Whiplash seeds it: the attitude
       * the machine was holding at the moment of contact becomes the
       * amplitude of a damped oscillation about the same two axes, and the
       * phase is restarted so the ring begins at full deflection. A flat
       * landing was barely pitched and barely rings; one off the side of a
       * hill was pitched a long way and rings for a second.
       */
      pMech->attitude.fWobblePitchAmp =
        (float)mecha_clampi(pMech->attitude.iAirPitch, -MECHA_WOBBLE_LIMIT,
                            MECHA_WOBBLE_LIMIT);
      pMech->attitude.fWobbleRollAmp =
        (float)mecha_clampi(pMech->attitude.iRollSteer, -MECHA_WOBBLE_LIMIT,
                            MECHA_WOBBLE_LIMIT);
      pMech->attitude.iWobblePhase = 0;
      pMech->attitude.iAirPitch = 0;
    }
    if ((pMech->byMove == MECHA_MOVE_JUMP
         || pMech->byMove == MECHA_MOVE_CANCEL) && bWasFalling) {
      bool bCancelled = pMech->byMove == MECHA_MOVE_CANCEL;

      pMech->byMove = MECHA_MOVE_LAND;
      /* A cancelled touchdown is the short one. Rather than carry a second
       * recovery length on every machine, start its clock partway through
       * the one they already have. */
      pMech->iStateTicks = bCancelled
        ? mecha_clampi(pDef->iLandTicks - MECHA_CANCEL_LAND_TICKS,
                       0, pDef->iLandTicks)
        : 0;
      if (bCancelled)
        pMech->iFreeTurnTicks = MECHA_CANCEL_TURN_TICKS;
      mecha_sim_spawn_effect(pWorld, MECHA_FX_DUST, pMech->fX, fGround,
                             pMech->fZ,
                             pDef->fRadius * (bCancelled ? 3.0f : 2.4f),
                             pDef->abyPalette[2], MECHA_SEC(0.45f));
    }
  }

  /*
   * Two ways to be gone that have nothing to do with damage.
   *
   * A pit in the race game is a surface like any other -- it answers a
   * height query, it is simply flagged as a pit and not drawn -- so a
   * machine standing over one has fallen in rather than fallen through.
   * And below the kill plane there is nothing at all, which is what
   * becomes of anything that walks off an open arena.
   */
  if (mecha_mech_alive(pMech)) {
    uint32_t uiSurface = mecha_arena_surface(&pWorld->arena, pMech->fX,
                                            pMech->fZ);
    bool bInPit = (uiSurface & MECHA_SURF_PIT) != 0
                  && pMech->fY <= fGround + MECHA_GROUND_EPS;

    if (bInPit || pMech->fY < pWorld->arena.fKillY) {
      mecha_sim_spawn_effect(pWorld, MECHA_FX_DUST, pMech->fX, pMech->fY,
                             pMech->fZ, pDef->fRadius * 2.0f,
                             pDef->abyPalette[2], MECHA_SEC(0.5f));
      /* Everything it had. Nobody is credited: the arena did this. */
      mecha_sim_damage(pWorld, iMechIdx, -1, pMech->fArmour + 1.0f,
                       MECHA_STAGGER_DOWN, 0.0f, 0.0f);
    }
  }
  pMech->fGroundY = fGround;

  /* --- cosmetic smoothing ---------------------------------------------- */

  mecha_update_attitude(pWorld, iMechIdx, pInput, bCanAct);

  {
    float fSpeed = mecha_length2(pMech->fVelX, pMech->fVelZ);
    float fLean = 0.0f;

    if (pMech->byMove == MECHA_MOVE_DASH)
      fLean = (float)MECHA_DEG(9);
    else if (pMech->byMove == MECHA_MOVE_WALK)
      fLean = (float)MECHA_DEG(3);
    pMech->fLeanRoll = mecha_approachf(pMech->fLeanRoll, fLean,
                                       (float)MECHA_DEG(40) * MECHA_DT);

    /*
     * Guns up, guns down. Bringing a weapon to bear is a snap and putting it
     * away is not, so the two rates are nothing like each other: a machine
     * that has just been shot at must not spend half a second raising its
     * arms, and a machine that has merely lost sight of someone must not
     * drop them the instant the lock breaks or the whole roster twitches.
     */
    {
      bool bReady = pMech->byLock == MECHA_LOCK_HELD || pMech->iRecovery > 0;

      pMech->fCombat = mecha_approachf(
          pMech->fCombat, bReady ? 1.0f : 0.0f,
          (bReady ? MECHA_COMBAT_RAISE : MECHA_COMBAT_LOWER) * MECHA_DT);
    }
    /*
     * Metres per stride, and it is deliberately long. The machines cover
     * ground faster than they used to and a cycle tied tightly to distance
     * turned that into a sprint of little steps; a longer stride reads as
     * something heavy moving quickly rather than as something small moving
     * frantically.
     */
    pMech->fStepPhase += fSpeed * MECHA_DT / MECHA_STRIDE_METRES;
    if (pMech->fStepPhase > 1000.0f)
      pMech->fStepPhase -= 1000.0f;

    /*
     * The feet follow the line of travel, not the direction of it: a machine
     * backing away from you is walking backwards, not turning round, so a
     * heading more than a quarter turn off the shoulders is folded back and
     * the step cycle runs in reverse instead. What is left is clamped, since
     * a mech whose feet point further off its shoulders than that is not
     * strafing, it is tangled.
     */
    if (pMech->byMove == MECHA_MOVE_DASH) {
      /*
       * A boost is not a strafe. The machine is being driven bodily in one
       * direction, so the legs square up to it however far round that is --
       * no fold, no clamp -- and the shoulders go on holding the aim, which
       * is the whole shape of the thing: running one way, shooting another.
       */
      int iTravel = mecha_atan2_angle(pMech->fDashDirX, pMech->fDashDirZ);

      pMech->bLegsBackward = false;
      pMech->iLegYaw = mecha_angle_approach(pMech->iLegYaw, iTravel,
                                            (int)(MECHA_LEG_DASH_RATE
                                                  * MECHA_DT));
    } else if (fSpeed > MECHA_LEG_WALK_SPEED) {
      int iTravel = mecha_atan2_angle(pMech->fVelX, pMech->fVelZ);
      int iOffset = mecha_angle_delta(pMech->iFacing, iTravel);

      pMech->bLegsBackward = iOffset > MECHA_ANGLE_QUARTER
                          || iOffset < -MECHA_ANGLE_QUARTER;
      if (pMech->bLegsBackward)
        iOffset = iOffset > 0 ? iOffset - MECHA_ANGLE_HALF
                              : iOffset + MECHA_ANGLE_HALF;
      iOffset = mecha_clampi(iOffset, -MECHA_LEG_YAW_LIMIT,
                             MECHA_LEG_YAW_LIMIT);
      iTravel = mecha_angle_wrap(pMech->iFacing + iOffset);
      pMech->iLegYaw = mecha_angle_approach(pMech->iLegYaw, iTravel,
                                            (int)(MECHA_LEG_YAW_RATE
                                                  * MECHA_DT));
    } else {
      pMech->bLegsBackward = false;
      pMech->iLegYaw = mecha_angle_approach(pMech->iLegYaw, pMech->iFacing,
                                            (int)(MECHA_LEG_YAW_RATE
                                                  * MECHA_DT));
    }
  }

  /* --- timers ----------------------------------------------------------- */

  if (pMech->iStunTicks > 0)
    pMech->iStunTicks--;
  if (pMech->iInvulnTicks > 0)
    pMech->iInvulnTicks--;
  if (pMech->iCoastTicks > 0)
    pMech->iCoastTicks--;
  pMech->fStagger = mecha_approachf(pMech->fStagger, 0.0f,
                                    MECHA_STAGGER_DECAY * MECHA_DT);

  pMech->bJumpHeld = pInput->bJump;
  pMech->bDashHeld = pInput->bDash;
  pMech->bGuardHeld = pInput->bGuard;
}

//-------------------------------------------------------------------------------------------------
/* Weapons */

static void mecha_direction_from_angles(int iYaw, int iPitch,
                                        float *pfX, float *pfY, float *pfZ)
{
  float fCosPitch = mecha_cos(iPitch);

  *pfX = mecha_sin(iYaw) * fCosPitch;
  *pfY = mecha_sin(iPitch);
  *pfZ = mecha_cos(iYaw) * fCosPitch;
}

//-------------------------------------------------------------------------------------------------

/* Launch elevation that drops a shell of this speed onto a target fDist away
 * and fRise higher, under fGravity. Picks the flat solution of the two, and
 * falls back to the maximum-range 45 degrees when the target is simply out
 * of reach. */
static int mecha_arc_pitch(float fDist, float fRise, float fSpeed,
                           float fGravity)
{
  float fSpeedSq;
  float fDiscriminant;
  float fTangent;

  if (fGravity <= 0.0f)
    return 0;
  if (fDist < 1.0f)
    return MECHA_ANGLE_QUARTER;

  fSpeedSq = fSpeed * fSpeed;
  fDiscriminant = fSpeedSq * fSpeedSq
                - fGravity * (fGravity * fDist * fDist + 2.0f * fRise * fSpeedSq);
  if (fDiscriminant < 0.0f)
    return MECHA_ANGLE_FULL / 8;

  fTangent = (fSpeedSq - sqrtf(fDiscriminant)) / (fGravity * fDist);
  return (int)(atanf(fTangent) * (float)MECHA_ANGLE_FULL
               / (2.0f * 3.14159265358979323846f));
}

//-------------------------------------------------------------------------------------------------

static tMechaProjectile *mecha_alloc_projectile(tMechaWorld *pWorld)
{
  tMechaProjectile *pOldest = NULL;
  int i;

  for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
    tMechaProjectile *pShot = &pWorld->aProjectiles[i];

    if (!pShot->bActive)
      return pShot;
    if (!pOldest || pShot->iLife < pOldest->iLife)
      pOldest = pShot;
  }
  /* Full: recycle whichever shot was closest to expiring anyway. A salvo
   * mech emptying every pod at once should never silently lose the shot the
   * player is watching for. */
  return pOldest;
}

//-------------------------------------------------------------------------------------------------

static void mecha_fire_weapon(tMechaWorld *pWorld, int iMechIdx, int iSlot)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_mech_def(pMech);
  eMechaStance eStance = mecha_mech_stance(pMech);
  const tMechaWeaponDef *pWeapon = &pDef->aWeapons[iSlot][eStance];
  const tMechaMech *pTarget = NULL;
  float fFwdX = mecha_sin(pMech->iFacing);
  float fFwdZ = mecha_cos(pMech->iFacing);
  float fOriginX;
  float fOriginY;
  float fOriginZ;
  int iBaseYaw;
  int iBasePitch;
  int iShot;

  if (pWeapon->fSpeed <= 0.0f || pWeapon->iAmmo <= 0)
    return;

  pMech->aiAmmo[iSlot]--;
  if (pMech->aiAmmo[iSlot] <= 0) {
    pMech->aiAmmo[iSlot] = 0;
    pMech->aiReload[iSlot] = pWeapon->iReloadTicks;
  }
  /*
   * One gun, three triggers, one magazine.
   *
   * A machine that carries a single weapon still has all three slots, so
   * it plays and reads like everything else on the roster -- but they are
   * three loads for the same gun, not three guns, and a magazine that
   * could be stretched by rolling across the other two triggers would not
   * be a magazine. So every round spent is spent out of all of them, and
   * they run dry and reload together.
   *
   * Which makes the choice a real one: nine rounds, and each is either
   * buckshot, a lance or a shell. Nothing about picking the third stops
   * the first two costing exactly as much.
   */
  if (pDef->bWheeled) {
    int iOther;

    for (iOther = 0; iOther < MECHA_WEAPON_SLOTS; iOther++) {
      if (iOther == iSlot)
        continue;
      pMech->aiAmmo[iOther] = pMech->aiAmmo[iSlot];
      if (pMech->aiReload[iOther] < pMech->aiReload[iSlot])
        pMech->aiReload[iOther] = pMech->aiReload[iSlot];
    }
  }
  /* And it shoves the machine. A gun the size of the car it is bolted to
   * does not go off quietly, and the kick is the other half of what makes
   * a long reload bearable: it buys distance. */
  if (pDef->fRecoilPush > 0.0f) {
    pMech->fVelX -= fFwdX * pDef->fRecoilPush;
    pMech->fVelZ -= fFwdZ * pDef->fRecoilPush;
  }
  pMech->iRecovery = pWeapon->iRecoveryTicks;
  pMech->iLastFiredSlot = iSlot;
  pMech->iLastFiredStance = (int)eStance;

  /* Muzzle: out along the shoulder the weapon hangs from, and forward far
   * enough that the shot clears the mech's own hull. */
  fOriginX = pMech->fX + fFwdZ * (pWeapon->fMuzzleSide * pDef->fRadius)
           + fFwdX * pDef->fRadius * 0.7f;
  fOriginZ = pMech->fZ - fFwdX * (pWeapon->fMuzzleSide * pDef->fRadius)
           + fFwdZ * pDef->fRadius * 0.7f;
  fOriginY = pMech->fY + pDef->fHeight * pWeapon->fMuzzleHeight;

  /*
   * Only a live lock aims the shot. With the lock broken the reticle is
   * still on someone, but the weapon knows nothing about them: it fires
   * straight down the barrel at whatever heading the mech is holding, with
   * no lead and no guidance. That is the whole point of the lock being
   * breakable -- losing it has to cost accuracy, not just the reticle.
   */
  if (pMech->byLock == MECHA_LOCK_HELD
      && pMech->iTargetIdx >= 0 && pMech->iTargetIdx < MECHA_MAX_MECHS
      && mecha_mech_alive(&pWorld->aMechs[pMech->iTargetIdx]))
    pTarget = &pWorld->aMechs[pMech->iTargetIdx];

  iBaseYaw = pMech->iFacing;
  iBasePitch = pMech->iAimPitch;

  if (pTarget) {
    float fAimX = pTarget->fX;
    float fAimZ = pTarget->fZ;
    float fAimY = mecha_mech_centre_height(pWorld, pMech->iTargetIdx);
    float fDx;
    float fDz;
    float fFlat;

    /* Lead the target by its own velocity over the time of flight. Beams are
     * effectively instant and homing shots correct themselves, so neither
     * needs it. */
    if (pWeapon->byKind == MECHA_PROJ_BULLET
        || pWeapon->byKind == MECHA_PROJ_ARC) {
      float fRough = mecha_length2(fAimX - fOriginX, fAimZ - fOriginZ);
      float fFlight = fRough / pWeapon->fSpeed;
      float fLead = pWeapon->byKind == MECHA_PROJ_ARC ? 1.0f : 0.85f;

      fAimX += pTarget->fVelX * fFlight * fLead;
      fAimZ += pTarget->fVelZ * fFlight * fLead;
    }

    fDx = fAimX - fOriginX;
    fDz = fAimZ - fOriginZ;
    fFlat = mecha_length2(fDx, fDz);
    iBaseYaw = mecha_atan2_angle(fDx, fDz);

    if (pWeapon->byKind == MECHA_PROJ_ARC && pWeapon->fArcGravity > 0.0f) {
      iBasePitch = mecha_arc_pitch(fFlat, fAimY - fOriginY, pWeapon->fSpeed,
                                   pWeapon->fArcGravity);
    } else if (fFlat > 1.0f) {
      iBasePitch = mecha_atan2_angle(fAimY - fOriginY, fFlat);
    }
  }

  if (pWeapon->byKind == MECHA_PROJ_MINE) {
    /* Mines are laid, not aimed: lobbed a short way out in front so they
     * cover the ground the shooter is about to give up. */
    iBaseYaw = pMech->iFacing;
    iBasePitch = mecha_arc_pitch(MECHA_M(25.0f), -pDef->fHeight * 0.34f,
                                 pWeapon->fSpeed, pWeapon->fArcGravity);
  }

  if (pWeapon->byKind == MECHA_PROJ_MELEE) {
    /* The swing drags the mech into it. */
    pMech->fDashDirX = mecha_sin(iBaseYaw);
    pMech->fDashDirZ = mecha_cos(iBaseYaw);
    pMech->fLungeSpeed = pWeapon->fSpeed;
    pMech->iLungeTicks = pWeapon->iLifeTicks;
    iBasePitch = 0;
  }

  mecha_sim_spawn_effect(pWorld, MECHA_FX_MUZZLE, fOriginX, fOriginY,
                         fOriginZ, pDef->fRadius * 0.5f, pWeapon->byPalette,
                         MECHA_SEC(0.12f));

  /*
   * Firing off a boost or out of the air brings the machine back onto its
   * lock. Firing while walking or standing does not, deliberately: those
   * are the states where the player is already free to point the thing,
   * and taking the heading away every time a trigger came down would be
   * the auto-turn back again wearing a different hat.
   */
  if (pMech->byMove == MECHA_MOVE_DASH || pMech->byMove == MECHA_MOVE_JUMP
      || pMech->byMove == MECHA_MOVE_CANCEL)
    pMech->iRecentreTicks = MECHA_RECENTRE_TICKS;

  /* Whatever the solution came out as, the pilot still has to hit with it.
   * Applied after the aim and before the spread so a wide burst is scattered
   * about the mistake rather than about the target. */
  iBaseYaw = mecha_angle_wrap(iBaseYaw + pMech->iAimError);

  for (iShot = 0; iShot < (int)pWeapon->byCount; iShot++) {
    tMechaProjectile *pShot = mecha_alloc_projectile(pWorld);
    /* Spread fans symmetrically about the aim: with one shot the offset is
     * zero, with two it straddles, with three the middle one runs true. */
    int iOffset = (2 * iShot - ((int)pWeapon->byCount - 1))
                  * pWeapon->iSpreadAngle / 2;
    float fDirX;
    float fDirY;
    float fDirZ;

    if (!pShot)
      break;

    mecha_direction_from_angles(mecha_angle_wrap(iBaseYaw + iOffset),
                                iBasePitch, &fDirX, &fDirY, &fDirZ);

    memset(pShot, 0, sizeof(*pShot));
    pShot->bActive = true;
    pShot->byKind = pWeapon->byKind;
    pShot->byOwner = (uint8_t)iMechIdx;
    pShot->byPalette = pWeapon->byPalette;
    pShot->fX = pShot->fPrevX = fOriginX;
    pShot->fY = pShot->fPrevY = fOriginY;
    pShot->fZ = pShot->fPrevZ = fOriginZ;
    pShot->fVelX = fDirX * pWeapon->fSpeed;
    pShot->fVelY = fDirY * pWeapon->fSpeed;
    pShot->fVelZ = fDirZ * pWeapon->fSpeed;
    pShot->fRadius = pWeapon->fRadius;
    pShot->fDamage = pWeapon->fDamage;
    pShot->fBlastRadius = pWeapon->fBlastRadius;
    pShot->fStagger = pWeapon->fStagger;
    pShot->fArcGravity = pWeapon->fArcGravity;
    pShot->iLife = pWeapon->iLifeTicks;
    pShot->iHomingRate = pWeapon->iHomingRate;
    /* A missile launched off a broken lock has nothing to home on. It is
     * still a missile; it just flies where it was pointed. */
    pShot->iTarget = pTarget ? pMech->iTargetIdx : -1;
    pShot->iArmTicks = pWeapon->byKind == MECHA_PROJ_MINE
                       ? MECHA_MINE_ARM_TICKS : 0;
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_update_weapons(tMechaWorld *pWorld, int iMechIdx,
                                 const tMechaInput *pInput, bool bCanAct)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_mech_def(pMech);
  bool abWanted[MECHA_WEAPON_SLOTS];
  int iSlot;

  if (pMech->iRecovery > 0)
    pMech->iRecovery--;
  if (pMech->iRamCooldown > 0)
    pMech->iRamCooldown--;

  for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
    if (pMech->aiReload[iSlot] > 0) {
      pMech->aiReload[iSlot]--;
      if (pMech->aiReload[iSlot] == 0) {
        /* Reloads restore the stance the weapon is in now, which is the same
         * magazine the next shot will draw from. */
        eMechaStance eStance = mecha_mech_stance(pMech);

        pMech->aiAmmo[iSlot] = pDef->aWeapons[iSlot][eStance].iAmmo;
      }
    }
  }

  abWanted[MECHA_SLOT_LEFT] = pInput->bFireLeft;
  abWanted[MECHA_SLOT_CENTER] = pInput->bFireCenter;
  abWanted[MECHA_SLOT_RIGHT] = pInput->bFireRight;

  for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
    bool bPressed = abWanted[iSlot] && !pMech->abFireHeld[iSlot];

    if (bCanAct && bPressed && pMech->iRecovery <= 0
        && pMech->aiAmmo[iSlot] > 0 && pMech->aiReload[iSlot] <= 0)
      mecha_fire_weapon(pWorld, iMechIdx, iSlot);
    pMech->abFireHeld[iSlot] = abWanted[iSlot];
  }
}

//-------------------------------------------------------------------------------------------------
/* Projectiles */

/* Closest approach of the segment to the mech's standing cylinder. Returns
 * true on contact and writes the fraction along the segment. */
static bool mecha_segment_hits_cylinder(float fX0, float fY0, float fZ0,
                                        float fX1, float fY1, float fZ1,
                                        float fShotRadius,
                                        float fCx, float fCy, float fCz,
                                        float fRadius, float fHeight,
                                        float *pfT)
{
  float fDx = fX1 - fX0;
  float fDz = fZ1 - fZ0;
  float fFx = fX0 - fCx;
  float fFz = fZ0 - fCz;
  float fSum = fShotRadius + fRadius;
  float fA = fDx * fDx + fDz * fDz;
  float fB = 2.0f * (fFx * fDx + fFz * fDz);
  float fC = fFx * fFx + fFz * fFz - fSum * fSum;
  float fEnter;
  float fExit;
  float fYa;
  float fYb;
  float fLow;
  float fHigh;

  if (fA < 1e-6f) {
    /* Standing still in the horizontal plane -- a mine, or a shot fired
     * straight up. */
    if (fC > 0.0f)
      return false;
    fEnter = 0.0f;
    fExit = 1.0f;
  } else {
    float fDiscriminant = fB * fB - 4.0f * fA * fC;
    float fRoot;

    if (fDiscriminant < 0.0f)
      return false;
    fRoot = sqrtf(fDiscriminant);
    fEnter = (-fB - fRoot) / (2.0f * fA);
    fExit = (-fB + fRoot) / (2.0f * fA);
    if (fExit < 0.0f || fEnter > 1.0f)
      return false;
    if (fEnter < 0.0f)
      fEnter = 0.0f;
    if (fExit > 1.0f)
      fExit = 1.0f;
  }

  /* Vertical overlap is checked across the whole span the shot spends inside
   * the circle, not just at the entry point, so a steeply falling shell
   * still connects. */
  fYa = fY0 + (fY1 - fY0) * fEnter;
  fYb = fY0 + (fY1 - fY0) * fExit;
  fLow = (fYa < fYb ? fYa : fYb) - fShotRadius;
  fHigh = (fYa > fYb ? fYa : fYb) + fShotRadius;
  if (fHigh < fCy || fLow > fCy + fHeight)
    return false;

  if (pfT)
    *pfT = fEnter;
  return true;
}

//-------------------------------------------------------------------------------------------------

/*
 * The fireball a blast leaves standing. The explosion itself has already
 * paid out its damage to everyone within reach, so those are marked as
 * burned before the shell has drawn a breath: what is left for it to do is
 * catch whoever walks in afterwards, and stop anything shot through it.
 */
static void mecha_spawn_shell(tMechaWorld *pWorld,
                              const tMechaProjectile *pSource)
{
  tMechaProjectile *pShell = mecha_alloc_projectile(pWorld);
  int i;

  if (!pShell)
    return;

  memset(pShell, 0, sizeof(*pShell));
  pShell->bActive = true;
  pShell->byKind = MECHA_PROJ_SHELL;
  pShell->byOwner = pSource->byOwner;
  pShell->byPalette = pSource->byPalette;
  pShell->fX = pSource->fX;
  pShell->fY = pSource->fY;
  pShell->fZ = pSource->fZ;
  pShell->fPrevX = pSource->fX;
  pShell->fPrevY = pSource->fY;
  pShell->fPrevZ = pSource->fZ;
  pShell->fBlastRadius = pSource->fBlastRadius;
  pShell->fRadius = pSource->fBlastRadius * MECHA_SHELL_OPEN;
  pShell->fDamage = pSource->fDamage;
  pShell->fStagger = pSource->fStagger;
  pShell->iLife = MECHA_SHELL_TICKS;
  pShell->iTarget = -1;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    const tMechaMech *pMech = &pWorld->aMechs[i];
    const tMechaMechDef *pDef;
    float fDist;

    if (!pMech->bActive)
      continue;
    if (i == (int)pShell->byOwner || !mecha_mech_alive(pMech)) {
      pShell->byHitMask |= (uint8_t)(1u << i);
      continue;
    }
    pDef = mecha_mech_def(pMech);
    fDist = mecha_length3(pMech->fX - pShell->fX,
                          (pMech->fY + pDef->fHeight * 0.5f) - pShell->fY,
                          pMech->fZ - pShell->fZ) - pDef->fRadius;
    if (fDist < pShell->fBlastRadius)
      pShell->byHitMask |= (uint8_t)(1u << i);
  }
}

//-------------------------------------------------------------------------------------------------

/* Opens the fireball a little further and burns anyone new inside it. */
static void mecha_update_shell(tMechaWorld *pWorld, tMechaProjectile *pShell)
{
  float fOpen;
  int i;

  pShell->iAge++;
  if (--pShell->iLife <= 0) {
    pShell->bActive = false;
    return;
  }

  fOpen = (float)pShell->iAge / (float)MECHA_SHELL_TICKS;
  if (fOpen > 1.0f)
    fOpen = 1.0f;
  pShell->fRadius = pShell->fBlastRadius
                    * (MECHA_SHELL_OPEN + (1.0f - MECHA_SHELL_OPEN) * fOpen);

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    tMechaMech *pMech = &pWorld->aMechs[i];
    const tMechaMechDef *pDef;
    float fDist;

    if ((pShell->byHitMask & (uint8_t)(1u << i)) != 0)
      continue;
    if (!mecha_mech_alive(pMech) || pMech->iInvulnTicks > 0)
      continue;
    pDef = mecha_mech_def(pMech);
    fDist = mecha_length3(pMech->fX - pShell->fX,
                          (pMech->fY + pDef->fHeight * 0.5f) - pShell->fY,
                          pMech->fZ - pShell->fZ) - pDef->fRadius;
    if (fDist >= pShell->fRadius)
      continue;

    pShell->byHitMask |= (uint8_t)(1u << i);
    mecha_sim_damage(pWorld, i, (int)pShell->byOwner,
                     pShell->fDamage * MECHA_SHELL_TOUCH,
                     pShell->fStagger * MECHA_SHELL_TOUCH, 0.0f, 0.0f);
    mecha_sim_spawn_effect(pWorld, MECHA_FX_IMPACT, pMech->fX,
                           pMech->fY + pDef->fHeight * 0.5f, pMech->fZ,
                           pDef->fRadius, pShell->byPalette,
                           MECHA_SEC(0.2f));
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_projectile_detonate(tMechaWorld *pWorld,
                                      tMechaProjectile *pShot,
                                      int iDirectVictim)
{
  if (pShot->fBlastRadius > 0.0f) {
    mecha_sim_explode(pWorld, (int)pShot->byOwner, pShot->fX, pShot->fY,
                      pShot->fZ, pShot->fBlastRadius, pShot->fDamage,
                      pShot->fStagger, pShot->byPalette);
    mecha_spawn_shell(pWorld, pShot);
  } else if (iDirectVictim >= 0) {
    float fLen = mecha_length3(pShot->fVelX, pShot->fVelY, pShot->fVelZ);
    float fPushX = 0.0f;
    float fPushZ = 0.0f;

    if (fLen > 1e-3f) {
      float fPush = pShot->fDamage * 0.06f * MECHA_METRE;

      fPushX = pShot->fVelX / fLen * fPush;
      fPushZ = pShot->fVelZ / fLen * fPush;
    }
    {
      float fDamageScale;
      float fStaggerScale;

      mecha_guard_mitigation(&pWorld->aMechs[iDirectVictim], pShot->byKind,
                             &fDamageScale, &fStaggerScale);
      mecha_sim_damage(pWorld, iDirectVictim, (int)pShot->byOwner,
                       pShot->fDamage * fDamageScale,
                       pShot->fStagger * fStaggerScale,
                       fPushX * fDamageScale, fPushZ * fDamageScale);
    }
    mecha_sim_spawn_effect(pWorld, MECHA_FX_IMPACT, pShot->fX, pShot->fY,
                           pShot->fZ, pShot->fRadius * 3.0f, pShot->byPalette,
                           MECHA_SEC(0.25f));
  } else {
    mecha_sim_spawn_effect(pWorld, MECHA_FX_SPARK, pShot->fX, pShot->fY,
                           pShot->fZ, pShot->fRadius * 2.0f, pShot->byPalette,
                           MECHA_SEC(0.2f));
  }
  pShot->bActive = false;
}

//-------------------------------------------------------------------------------------------------

static void mecha_home_projectile(tMechaWorld *pWorld,
                                  tMechaProjectile *pShot)
{
  const tMechaMech *pTarget;
  float fSpeed;
  float fDx;
  float fDy;
  float fDz;
  float fFlat;
  int iYaw;
  int iPitch;
  int iWantYaw;
  int iWantPitch;

  if (pShot->iHomingRate <= 0)
    return;
  if (pShot->iTarget < 0 || pShot->iTarget >= MECHA_MAX_MECHS)
    return;
  pTarget = &pWorld->aMechs[pShot->iTarget];
  if (!mecha_mech_alive(pTarget))
    return;

  fSpeed = mecha_length3(pShot->fVelX, pShot->fVelY, pShot->fVelZ);
  if (fSpeed < 1e-3f)
    return;

  fDx = pTarget->fX - pShot->fX;
  fDz = pTarget->fZ - pShot->fZ;
  fDy = mecha_mech_centre_height(pWorld, pShot->iTarget) - pShot->fY;
  fFlat = mecha_length2(fDx, fDz);

  iYaw = mecha_atan2_angle(pShot->fVelX, pShot->fVelZ);
  iPitch = mecha_atan2_angle(pShot->fVelY,
                             mecha_length2(pShot->fVelX, pShot->fVelZ));
  iWantYaw = mecha_atan2_angle(fDx, fDz);
  iWantPitch = fFlat > 1.0f ? mecha_atan2_angle(fDy, fFlat)
                            : (fDy >= 0.0f ? MECHA_ANGLE_QUARTER
                                           : -MECHA_ANGLE_QUARTER);

  iYaw = mecha_angle_approach(iYaw, iWantYaw, pShot->iHomingRate);
  iPitch = mecha_angle_approach(iPitch, iWantPitch, pShot->iHomingRate);

  {
    float fDirX;
    float fDirY;
    float fDirZ;

    mecha_direction_from_angles(iYaw, iPitch, &fDirX, &fDirY, &fDirZ);
    pShot->fVelX = fDirX * fSpeed;
    pShot->fVelY = fDirY * fSpeed;
    pShot->fVelZ = fDirZ * fSpeed;
  }
}

//-------------------------------------------------------------------------------------------------

/* Shots that meet in the air settle it between themselves. A mine sitting
 * on the floor and a swing carried in front of a machine are neither of
 * them things in flight, so neither takes part. */
static bool mecha_shot_trades(const tMechaProjectile *pShot)
{
  return pShot->byKind != MECHA_PROJ_MINE
      && pShot->byKind != MECHA_PROJ_MELEE;
}

//-------------------------------------------------------------------------------------------------

/*
 * Fire against fire.
 *
 * Two shots that meet are worth what they do: within a sixth of each other
 * they trade, both gone, and outside that the heavier one carries on
 * through unchanged. It is what makes a siege shell worth the wind-up and a
 * spread worth firing at one -- and it is why a wall of fire is a wall
 * rather than a suggestion.
 *
 * Anything carrying a blast goes off where it was stopped rather than
 * blinking out, so shooting a bomb down is a decision about where it
 * explodes rather than whether it does.
 */
static void mecha_trade_projectiles(tMechaWorld *pWorld)
{
  int i;
  int j;

  for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
    tMechaProjectile *pA = &pWorld->aProjectiles[i];

    if (!pA->bActive || !mecha_shot_trades(pA))
      continue;

    for (j = i + 1; j < MECHA_MAX_PROJECTILES; j++) {
      tMechaProjectile *pB = &pWorld->aProjectiles[j];
      bool bShellA = pA->byKind == MECHA_PROJ_SHELL;
      bool bShellB = pB->byKind == MECHA_PROJ_SHELL;
      float fReach;
      float fGap;
      float fHigher;

      if (!pB->bActive || !mecha_shot_trades(pB))
        continue;
      /* A fireball is everybody's problem; two shots from the same machine
       * are nobody's. */
      if (!bShellA && !bShellB && pA->byOwner == pB->byOwner)
        continue;
      if (bShellA && bShellB)
        continue;

      fReach = pA->fRadius + pB->fRadius;
      if (mecha_length3(pA->fX - pB->fX, pA->fY - pB->fY, pA->fZ - pB->fZ)
          > fReach)
        continue;

      if (bShellA) {
        mecha_projectile_detonate(pWorld, pB, -1);
        continue;
      }
      if (bShellB) {
        mecha_projectile_detonate(pWorld, pA, -1);
        break;
      }

      fHigher = pA->fDamage > pB->fDamage ? pA->fDamage : pB->fDamage;
      fGap = pA->fDamage - pB->fDamage;
      if (fGap < 0.0f)
        fGap = -fGap;

      if (fGap <= fHigher * MECHA_SHOT_TRADE_MARGIN) {
        mecha_projectile_detonate(pWorld, pB, -1);
        mecha_projectile_detonate(pWorld, pA, -1);
        break;
      }
      if (pA->fDamage > pB->fDamage) {
        mecha_projectile_detonate(pWorld, pB, -1);
        continue;
      }
      mecha_projectile_detonate(pWorld, pA, -1);
      break;
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_update_projectiles(tMechaWorld *pWorld)
{
  int i;

  for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
    tMechaProjectile *pShot = &pWorld->aProjectiles[i];
    bool bMine;
    bool bLanded = false;
    float fGround;
    int iVictim = -1;
    float fBestT = 2.0f;
    int iMech;

    if (!pShot->bActive)
      continue;

    if (pShot->byKind == MECHA_PROJ_SHELL) {
      mecha_update_shell(pWorld, pShot);
      continue;
    }

    bMine = pShot->byKind == MECHA_PROJ_MINE;

    pShot->fPrevX = pShot->fX;
    pShot->fPrevY = pShot->fY;
    pShot->fPrevZ = pShot->fZ;

    if (pShot->iArmTicks > 0)
      pShot->iArmTicks--;
    pShot->iAge++;

    if (pShot->byKind == MECHA_PROJ_HOMING)
      mecha_home_projectile(pWorld, pShot);
    if (pShot->fArcGravity > 0.0f)
      pShot->fVelY -= pShot->fArcGravity * MECHA_DT;

    pShot->fX += pShot->fVelX * MECHA_DT;
    pShot->fY += pShot->fVelY * MECHA_DT;
    pShot->fZ += pShot->fVelZ * MECHA_DT;

    if (--pShot->iLife <= 0) {
      /* Anything carrying a blast goes off when it times out -- that is what
       * makes a mine a mine. A plain tracer or a missed swing just stops
       * existing, with no puff to suggest it hit something. */
      if (pShot->fBlastRadius > 0.0f)
        mecha_projectile_detonate(pWorld, pShot, -1);
      else
        pShot->bActive = false;
      continue;
    }

    /* A laid mine settles on the ground and waits there. */
    if (bMine) {
      fGround = mecha_arena_ground_height(&pWorld->arena, pShot->fX,
                                          pShot->fZ, pShot->fY);
      if (pShot->fY <= fGround) {
        pShot->fY = fGround;
        pShot->fVelX = 0.0f;
        pShot->fVelY = 0.0f;
        pShot->fVelZ = 0.0f;
        pShot->fArcGravity = 0.0f;
        bLanded = true;
      }
    }

    /* Mechs. The owner is never a valid target for its own shot. */
    for (iMech = 0; iMech < MECHA_MAX_MECHS; iMech++) {
      const tMechaMech *pMech = &pWorld->aMechs[iMech];
      const tMechaMechDef *pMechDef;
      float fT;

      if (iMech == (int)pShot->byOwner || !mecha_mech_alive(pMech))
        continue;
      if (pMech->iInvulnTicks > 0)
        continue;

      pMechDef = mecha_def_get((int)pMech->byDefIdx);
      if (bMine) {
        /* An armed mine triggers on proximity rather than on contact. */
        float fTrigger;

        if (pShot->iArmTicks > 0 || !bLanded)
          continue;
        fTrigger = pShot->fBlastRadius * 0.55f + pMechDef->fRadius;
        if (mecha_length2(pMech->fX - pShot->fX, pMech->fZ - pShot->fZ)
              <= fTrigger
            && pMech->fY <= pShot->fY + pShot->fBlastRadius) {
          iVictim = iMech;
          fBestT = 0.0f;
          break;
        }
        continue;
      }

      if (mecha_segment_hits_cylinder(pShot->fPrevX, pShot->fPrevY,
                                      pShot->fPrevZ, pShot->fX, pShot->fY,
                                      pShot->fZ, pShot->fRadius,
                                      pMech->fX, pMech->fY, pMech->fZ,
                                      pMechDef->fRadius, pMechDef->fHeight,
                                      &fT)
          && fT < fBestT) {
        fBestT = fT;
        iVictim = iMech;
      }
    }

    if (iVictim >= 0) {
      if (fBestT <= 1.0f) {
        pShot->fX = pShot->fPrevX + (pShot->fX - pShot->fPrevX) * fBestT;
        pShot->fY = pShot->fPrevY + (pShot->fY - pShot->fPrevY) * fBestT;
        pShot->fZ = pShot->fPrevZ + (pShot->fZ - pShot->fPrevZ) * fBestT;
      }
      mecha_projectile_detonate(pWorld, pShot, iVictim);
      continue;
    }

    /* A melee hitbox is a swing, not an object: it sweeps through cover. */
    if (pShot->byKind == MECHA_PROJ_MELEE)
      continue;
    if (bMine && bLanded)
      continue;

    {
      float fHitX;
      float fHitY;
      float fHitZ;

      if (mecha_arena_trace_segment(&pWorld->arena,
                                    pShot->fPrevX, pShot->fPrevY,
                                    pShot->fPrevZ, pShot->fX, pShot->fY,
                                    pShot->fZ, &fHitX, &fHitY, &fHitZ)) {
        if (bMine) {
          /* A mine that hit a wall on the way out just drops there. */
          pShot->fX = fHitX;
          pShot->fY = fHitY;
          pShot->fZ = fHitZ;
          pShot->fVelX = 0.0f;
          pShot->fVelY = 0.0f;
          pShot->fVelZ = 0.0f;
          pShot->fArcGravity = 0.0f;
          continue;
        }
        pShot->fX = fHitX;
        pShot->fY = fHitY;
        pShot->fZ = fHitZ;
        mecha_projectile_detonate(pWorld, pShot, -1);
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_update_effects(tMechaWorld *pWorld)
{
  int i;

  for (i = 0; i < MECHA_MAX_EFFECTS; i++) {
    tMechaEffect *pFx = &pWorld->aEffects[i];

    if (!pFx->bActive)
      continue;
    if (++pFx->iAge >= pFx->iLife) {
      pFx->bActive = false;
      continue;
    }
    /* Debris falls; every other effect drifts wherever it was sent. */
    if (pFx->byKind == MECHA_FX_EMBER)
      pFx->fVelY -= MECHA_GRAVITY * MECHA_DT;
    pFx->fX += pFx->fVelX * MECHA_DT;
    pFx->fY += pFx->fVelY * MECHA_DT;
    pFx->fZ += pFx->fVelZ * MECHA_DT;
  }
}

/*
 * One machine running into another. fClosing is how fast the gap between
 * them is shutting, and the direction is from the rammer towards the rammed.
 * Nothing happens unless the rammer is on wheels, is doing it fast enough to
 * be worth anything, and is actually driving into them rather than being
 * shunted by them: a car that has just been knocked into somebody has not
 * run them over.
 */
static void mecha_try_ram(tMechaWorld *pWorld, int iRammer, int iVictim,
                          const tMechaMechDef *pDef, float fClosing,
                          float fDirX, float fDirZ)
{
  tMechaMech *pRammer = &pWorld->aMechs[iRammer];
  float fNoseX;
  float fNoseZ;
  float fOver;

  if (!pDef->bWheeled || pDef->fRamDamage <= 0.0f
      || pRammer->iRamCooldown > 0)
    return;
  if (fClosing <= pDef->fRamSpeed)
    return;

  /* Driving into them: the closing has to be happening down the nose, not
   * sideways and not backwards. */
  fNoseX = mecha_sin(pRammer->iFacing);
  fNoseZ = mecha_cos(pRammer->iFacing);
  if (fNoseX * fDirX + fNoseZ * fDirZ < MECHA_CAR_RAM_DOT)
    return;

  fOver = (fClosing - pDef->fRamSpeed) / MECHA_METRE;
  pRammer->iRamCooldown = MECHA_CAR_RAM_TICKS;
  mecha_sim_damage(pWorld, iVictim, iRammer, fOver * pDef->fRamDamage,
                   fOver * pDef->fRamDamage * MECHA_CAR_RAM_STAGGER,
                   fDirX, fDirZ);
  mecha_sim_spawn_effect(pWorld, MECHA_FX_DUST, pRammer->fX,
                         pRammer->fY + pDef->fHeight * 0.5f, pRammer->fZ,
                         pDef->fRadius * 1.6f, pDef->abyPalette[3],
                         MECHA_SEC(0.35f));
}

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------

/* Mechs are solid to each other: they shove rather than interpenetrate, with
 * the lighter one giving most of the ground. */
static void mecha_resolve_overlaps(tMechaWorld *pWorld)
{
  int i;
  int j;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    tMechaMech *pA = &pWorld->aMechs[i];
    const tMechaMechDef *pDefA;

    if (!mecha_mech_alive(pA))
      continue;
    pDefA = mecha_mech_def(pA);

    for (j = i + 1; j < MECHA_MAX_MECHS; j++) {
      tMechaMech *pB = &pWorld->aMechs[j];
      const tMechaMechDef *pDefB;
      float fDx;
      float fDz;
      float fDist;
      float fWant;
      float fPush;
      float fShareA;

      if (!mecha_mech_alive(pB))
        continue;
      pDefB = mecha_mech_def(pB);

      /* One standing on the other's roof is not an overlap. */
      if (pA->fY >= pB->fY + pDefB->fHeight
          || pB->fY >= pA->fY + pDefA->fHeight)
        continue;

      fDx = pB->fX - pA->fX;
      fDz = pB->fZ - pA->fZ;
      fDist = mecha_length2(fDx, fDz);
      fWant = pDefA->fRadius + pDefB->fRadius;
      if (fDist >= fWant)
        continue;

      if (fDist < 1e-3f) {
        /* Exactly stacked: pick a deterministic direction rather than
         * dividing by zero. */
        fDx = 1.0f;
        fDz = 0.0f;
        fDist = 1.0f;
      }
      fPush = fWant - fDist;
      if (fPush > MECHA_PUSH_PER_TICK)
        fPush = MECHA_PUSH_PER_TICK;
      fShareA = pDefB->fMass / (pDefA->fMass + pDefB->fMass);

      pA->fX -= fDx / fDist * fPush * fShareA;
      pA->fZ -= fDz / fDist * fPush * fShareA;
      pB->fX += fDx / fDist * fPush * (1.0f - fShareA);
      pB->fZ += fDz / fDist * fPush * (1.0f - fShareA);

      mecha_arena_resolve_cylinder(&pWorld->arena, pDefA->fRadius, pA->fY,
                                   pDefA->fHeight, &pA->fX, &pA->fZ);
      mecha_arena_resolve_cylinder(&pWorld->arena, pDefB->fRadius, pB->fY,
                                   pDefB->fHeight, &pB->fX, &pB->fZ);

      /*
       * And a machine that runs on wheels can run somebody over.
       *
       * It is the only thing this one has at close quarters -- it carries
       * no melee row at all -- so it has to hurt, and it is charged on the
       * speed it is closing at rather than on its own speed: driving
       * alongside somebody is not a ram, and a head-on is worse than
       * catching them up. Both machines can be doing it at once, which is
       * fair, and neither can do it to a friend.
       */
      if (pA->byTeam != pB->byTeam) {
        /* How fast the gap is shutting: the relative velocity resolved
         * along the line between them, positive when they are coming
         * together. Symmetric, so both of them get the same number. */
        float fCloseX = pA->fVelX - pB->fVelX;
        float fCloseZ = pA->fVelZ - pB->fVelZ;
        float fClosing = (fCloseX * fDx + fCloseZ * fDz) / fDist;

        mecha_try_ram(pWorld, i, j, pDefA, fClosing, fDx / fDist,
                      fDz / fDist);
        mecha_try_ram(pWorld, j, i, pDefB, fClosing, -fDx / fDist,
                      -fDz / fDist);
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
/* Rounds and match flow */

static void mecha_reset_mech_for_round(tMechaWorld *pWorld, int iMechIdx,
                                       int iSlot, int iSlotCount)
{
  tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_mech_def(pMech);
  float fX = 0.0f;
  float fZ = 0.0f;
  int iFacing = 0;
  int i;

  mecha_arena_spawn_point(&pWorld->arena, iSlot, iSlotCount, &fX, &fZ,
                          &iFacing);

  pMech->fX = fX;
  pMech->fZ = fZ;
  pMech->fY = mecha_arena_ground_height(&pWorld->arena, fX, fZ, 0.0f);
  /* Where the ground is, as far as the ramp rule is concerned. Leaving this
   * at zero on a hill would read as the ground having risen the whole height
   * of the hill in one tick, and fire the machine into the air on the first
   * frame of the round. */
  pMech->fGroundY = pMech->fY;
  pMech->fVelX = 0.0f;
  pMech->fVelY = 0.0f;
  pMech->fVelZ = 0.0f;
  pMech->iFacing = iFacing;
  pMech->iAimPitch = 0;

  pMech->byMove = MECHA_MOVE_STAND;
  pMech->iStateTicks = 0;
  pMech->iBoost = pDef->iBoostMax * MECHA_BOOST_SCALE;
  pMech->bBoostLocked = false;
  pMech->fDashDirX = 0.0f;
  pMech->fDashDirZ = 0.0f;
  pMech->iCoastTicks = 0;
  pMech->bDashStickFree = false;

  pMech->fArmour = pDef->fArmour;
  pMech->fStagger = 0.0f;
  pMech->iStunTicks = 0;
  pMech->iInvulnTicks = 0;
  pMech->iRecovery = 0;
  pMech->iLungeTicks = 0;
  pMech->fLungeSpeed = 0.0f;
  pMech->iLastFiredSlot = -1;
  pMech->iLastFiredStance = MECHA_STANCE_STAND;
  pMech->iTargetIdx = -1;
  pMech->byLock = MECHA_LOCK_NONE;
  pMech->iLockSlipTicks = 0;
  pMech->iFreeTurnTicks = 0;
  pMech->iRecentreTicks = 0;

  for (i = 0; i < MECHA_WEAPON_SLOTS; i++) {
    /* Magazines are per slot, not per stance: the standing loadout is what a
     * round opens with, and a later reload refills to whatever stance the
     * mech is in when it completes. */
    pMech->aiAmmo[i] = pDef->aWeapons[i][MECHA_STANCE_STAND].iAmmo;
    pMech->aiReload[i] = 0;
    pMech->abFireHeld[i] = false;
  }
  pMech->bJumpHeld = false;
  pMech->bDashHeld = false;
  pMech->bGuardHeld = false;
  pMech->bCycleHeld = false;

  pMech->fLeanRoll = 0.0f;
  pMech->fCombat = 0.0f;
  pMech->fStepPhase = 0.0f;
  pMech->iLegYaw = pMech->iFacing;
  pMech->bLegsBackward = false;

  memset(&pMech->attitude, 0, sizeof(pMech->attitude));
  /* Its own stream, and a different one per machine, so four identical
   * cars in a row do not rattle in unison. */
  mecha_rng_seed(&pMech->attitude.shake,
                 0x9E3779B9u * (uint32_t)(iMechIdx + 1) + 0x51ED270Bu);
}

//-------------------------------------------------------------------------------------------------

static void mecha_reset_round(tMechaWorld *pWorld)
{
  int iSlot = 0;
  int i;

  memset(pWorld->aProjectiles, 0, sizeof(pWorld->aProjectiles));
  memset(pWorld->aEffects, 0, sizeof(pWorld->aEffects));

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    if (pWorld->aMechs[i].bActive)
      mecha_reset_mech_for_round(pWorld, i, iSlot++, pWorld->iMechCount);
  }

  pWorld->match.byPhase = MECHA_PHASE_READY;
  pWorld->match.iPhaseTicks = MECHA_READY_TICKS;
  pWorld->match.iRoundTicks = pWorld->match.iRoundTimeLimit;
  pWorld->match.iWinnerIdx = -1;
}


//-------------------------------------------------------------------------------------------------

/* Survivors, and the strongest survivor, for one team. */
static int mecha_team_survivors(const tMechaWorld *pWorld, uint8_t byTeam,
                                int *piBest)
{
  float fBestArmour = -1.0f;
  int iCount = 0;
  int i;

  if (piBest)
    *piBest = -1;
  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    const tMechaMech *pMech = &pWorld->aMechs[i];

    if (!mecha_mech_alive(pMech) || pMech->byTeam != byTeam)
      continue;
    iCount++;
    if (piBest && pMech->fArmour > fBestArmour) {
      fBestArmour = pMech->fArmour;
      *piBest = i;
    }
  }
  return iCount;
}

//-------------------------------------------------------------------------------------------------

static float mecha_team_armour(const tMechaWorld *pWorld, uint8_t byTeam)
{
  float fTotal = 0.0f;
  int i;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    if (pWorld->aMechs[i].bActive && pWorld->aMechs[i].byTeam == byTeam)
      fTotal += mecha_mech_armour_fraction(pWorld, i);
  }
  return fTotal;
}

//-------------------------------------------------------------------------------------------------

static void mecha_end_round(tMechaWorld *pWorld, int iWinnerIdx)
{
  pWorld->match.iWinnerIdx = iWinnerIdx;
  pWorld->match.byPhase = MECHA_PHASE_ROUND_OVER;
  pWorld->match.iPhaseTicks = MECHA_ROUND_OVER_TICKS;

  if (iWinnerIdx >= 0 && iWinnerIdx < MECHA_MAX_MECHS
      && pWorld->aMechs[iWinnerIdx].bActive)
    pWorld->aMechs[iWinnerIdx].iRoundsWon++;
}

//-------------------------------------------------------------------------------------------------

static void mecha_check_round_end(tMechaWorld *pWorld)
{
  uint8_t abySeen[MECHA_MAX_MECHS];
  int iTeamCount = 0;
  int iLiveTeams = 0;
  int iLastLiveTeam = -1;
  int i;

  /* Which teams are still standing. Teams are small integers picked by the
   * caller, so they are gathered rather than assumed to be 0 and 1. */
  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    const tMechaMech *pMech = &pWorld->aMechs[i];
    int j;
    bool bKnown = false;

    if (!pMech->bActive)
      continue;
    for (j = 0; j < iTeamCount; j++) {
      if (abySeen[j] == pMech->byTeam) {
        bKnown = true;
        break;
      }
    }
    if (!bKnown)
      abySeen[iTeamCount++] = pMech->byTeam;
  }

  for (i = 0; i < iTeamCount; i++) {
    if (mecha_team_survivors(pWorld, abySeen[i], NULL) > 0) {
      iLiveTeams++;
      iLastLiveTeam = (int)abySeen[i];
    }
  }

  if (iLiveTeams == 1 && iTeamCount > 1) {
    int iBest = -1;

    mecha_team_survivors(pWorld, (uint8_t)iLastLiveTeam, &iBest);
    mecha_end_round(pWorld, iBest);
    return;
  }
  if (iLiveTeams == 0) {
    /* Double knockout. */
    mecha_end_round(pWorld, -1);
    return;
  }

  if (pWorld->match.iRoundTimeLimit > 0 && pWorld->match.iRoundTicks <= 0) {
    /* Time up: the team with the most armour left takes it. */
    float fBestArmour = -1.0f;
    int iBestTeam = -1;
    bool bTie = false;

    for (i = 0; i < iTeamCount; i++) {
      float fArmour = mecha_team_armour(pWorld, abySeen[i]);

      if (fArmour > fBestArmour + 1e-4f) {
        fBestArmour = fArmour;
        iBestTeam = (int)abySeen[i];
        bTie = false;
      } else if (fabsf(fArmour - fBestArmour) <= 1e-4f) {
        bTie = true;
      }
    }

    if (bTie || iBestTeam < 0) {
      mecha_end_round(pWorld, -1);
    } else {
      int iBest = -1;

      mecha_team_survivors(pWorld, (uint8_t)iBestTeam, &iBest);
      mecha_end_round(pWorld, iBest);
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_advance_phase(tMechaWorld *pWorld)
{
  tMechaMatch *pMatch = &pWorld->match;
  int i;

  switch (pMatch->byPhase) {
  case MECHA_PHASE_READY:
    if (--pMatch->iPhaseTicks <= 0) {
      pMatch->byPhase = MECHA_PHASE_FIGHT;
      pMatch->iPhaseTicks = 0;
    }
    break;

  case MECHA_PHASE_FIGHT:
    /*
     * Down to zero on a clock, up from it on a deathmatch. The count still
     * runs either way because things other than the end of the round read
     * it -- the FIGHT banner wants to know how long ago the round started,
     * and on a deathmatch a clock frozen at zero could never tell it.
     */
    if (pMatch->iRoundTimeLimit > 0) {
      if (pMatch->iRoundTicks > 0)
        pMatch->iRoundTicks--;
    } else {
      pMatch->iRoundTicks++;
    }
    break;

  case MECHA_PHASE_ROUND_OVER:
    if (--pMatch->iPhaseTicks > 0)
      break;
    for (i = 0; i < MECHA_MAX_MECHS; i++) {
      if (pWorld->aMechs[i].bActive
          && pWorld->aMechs[i].iRoundsWon >= pMatch->iRoundsToWin) {
        pMatch->byPhase = MECHA_PHASE_MATCH_OVER;
        pMatch->iWinnerIdx = i;
        return;
      }
    }
    pMatch->iRound++;
    mecha_reset_round(pWorld);
    break;

  default:
    break;
  }
}

//-------------------------------------------------------------------------------------------------
/* Public entry points */

void mecha_sim_init(tMechaWorld *pWorld, int iArenaIdx, uint32_t uiSeed,
                    int iRoundsToWin)
{
  if (!pWorld)
    return;

  memset(pWorld, 0, sizeof(*pWorld));
  mecha_arena_init(&pWorld->arena, iArenaIdx);
  mecha_rng_seed(&pWorld->rng, uiSeed);
  pWorld->uiSeed = uiSeed;

  pWorld->match.iRoundsToWin = iRoundsToWin > 0 ? iRoundsToWin : 1;
  pWorld->match.iRoundTimeLimit = MECHA_SEC((float)MECHA_ROUND_SECONDS);
  pWorld->match.iRoundTicks = pWorld->match.iRoundTimeLimit;
  pWorld->match.iRound = 1;
  pWorld->match.byPhase = MECHA_PHASE_READY;
  pWorld->match.iPhaseTicks = MECHA_READY_TICKS;
  pWorld->match.iWinnerIdx = -1;

  /* Not the top of the ladder. ACE is the pilot with no reaction time and no
   * aim error, and a first-time player has no answer to it. */
  pWorld->byAiSkill = MECHA_AI_VETERAN;
}

//-------------------------------------------------------------------------------------------------

void mecha_sim_set_ai_skill(tMechaWorld *pWorld, int iSkill)
{
  if (!pWorld)
    return;
  if (iSkill < 0)
    iSkill = 0;
  if (iSkill >= MECHA_AI_SKILL_COUNT)
    iSkill = MECHA_AI_SKILL_COUNT - 1;
  pWorld->byAiSkill = (uint8_t)iSkill;
}

//-------------------------------------------------------------------------------------------------

void mecha_sim_set_round_seconds(tMechaWorld *pWorld, int iSeconds)
{
  if (!pWorld)
    return;
  /*
   * Zero is a deathmatch: the clock is switched off rather than set very
   * high, because a round decided on armour when the clock runs out is a
   * different game from one that only ends when somebody falls over, and a
   * very long clock is still the first of those.
   */
  pWorld->match.iRoundTimeLimit = iSeconds > 0
                                      ? MECHA_SEC((float)iSeconds) : 0;
  pWorld->match.iRoundTicks = pWorld->match.iRoundTimeLimit;
}

//-------------------------------------------------------------------------------------------------

void mecha_sim_set_ai_hold_fire(tMechaWorld *pWorld, bool bHold)
{
  if (pWorld)
    pWorld->bAiHoldFire = bHold;
}

//-------------------------------------------------------------------------------------------------

const char *mecha_sim_ai_skill_name(int iSkill)
{
  switch (iSkill) {
  case MECHA_AI_ROOKIE:  return "ROOKIE";
  case MECHA_AI_VETERAN: return "VETERAN";
  case MECHA_AI_ACE:     return "ACE";
  default:               return "?";
  }
}

//-------------------------------------------------------------------------------------------------

int mecha_sim_add_mech(tMechaWorld *pWorld, int iDefIdx,
                       uint8_t byController, uint8_t byTeam)
{
  int i;

  if (!pWorld)
    return -1;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    tMechaMech *pMech = &pWorld->aMechs[i];

    if (pMech->bActive)
      continue;

    memset(pMech, 0, sizeof(*pMech));
    pMech->bActive = true;
    pMech->byDefIdx = (uint8_t)(iDefIdx < 0 ? 0
                                : (iDefIdx % mecha_def_count()));
    pMech->byController = byController;
    pMech->byTeam = byTeam;
    pMech->iTargetIdx = -1;
  pMech->byLock = MECHA_LOCK_NONE;
  pMech->iLockSlipTicks = 0;
  pMech->iFreeTurnTicks = 0;
  pMech->iRecentreTicks = 0;
    pMech->iLastFiredSlot = -1;
    pMech->fArmour = mecha_def_get(pMech->byDefIdx)->fArmour;
    pWorld->iMechCount++;
    return i;
  }
  return -1;
}

//-------------------------------------------------------------------------------------------------

void mecha_sim_begin_match(tMechaWorld *pWorld)
{
  int i;

  if (!pWorld)
    return;

  for (i = 0; i < MECHA_MAX_MECHS; i++)
    pWorld->aMechs[i].iRoundsWon = 0;
  pWorld->match.iRound = 1;
  pWorld->iTick = 0;
  mecha_reset_round(pWorld);
}

//-------------------------------------------------------------------------------------------------

void mecha_sim_tick(tMechaWorld *pWorld, const tMechaInput *paInputs,
                    int iInputCount)
{
  static const tMechaInput kIdle;
  bool bControlsLive;
  int i;

  if (!pWorld)
    return;

  pWorld->iTick++;
  mecha_advance_phase(pWorld);

  /* Controls are only live during the fight itself. Everything else still
   * ticks -- shots already in the air finish their flight, explosions play
   * out -- so a round that ends mid-salvo looks like one. */
  bControlsLive = pWorld->match.byPhase == MECHA_PHASE_FIGHT;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    tMechaMech *pMech = &pWorld->aMechs[i];
    tMechaInput input;
    bool bCanAct;

    if (!pMech->bActive)
      continue;

    if (!bControlsLive) {
      input = kIdle;
    } else if (pMech->byController == MECHA_CONTROL_AI) {
      mecha_ai_think(pWorld, i, &input);
    } else if (paInputs && i < iInputCount) {
      input = paInputs[i];
    } else {
      input = kIdle;
    }

    bCanAct = bControlsLive && mecha_can_act(pMech);

    mecha_update_target(pWorld, i, input.bCycleTarget && !pMech->bCycleHeld);
    pMech->bCycleHeld = input.bCycleTarget;
    mecha_update_lock(pWorld, i);

    mecha_update_facing(pWorld, i, &input, bCanAct);
    mecha_update_movement(pWorld, i, &input, bCanAct);
    mecha_update_weapons(pWorld, i, &input, bCanAct);
  }

  mecha_update_projectiles(pWorld);
  /* After they have moved, so two shots closing head on meet where they
   * actually met rather than a tick either side of it. */
  mecha_trade_projectiles(pWorld);
  mecha_update_effects(pWorld);
  mecha_resolve_overlaps(pWorld);

  if (pWorld->match.byPhase == MECHA_PHASE_FIGHT)
    mecha_check_round_end(pWorld);
}
