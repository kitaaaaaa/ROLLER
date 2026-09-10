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
    if (mecha_target_range(pWorld, iMechIdx) <= MECHA_CLOSE_QUARTERS
        || pMech->iRecentreTicks > 0) {
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
  if ((bCanAct || bFreeTurn) && pInput->iTurn != 0) {
    int iManual = (int)(pDef->fTurnRate * MECHA_DT
                        * (float)pInput->iTurn / 100.0f);

    if (bFreeTurn)
      iManual *= MECHA_CANCEL_TURN_SCALE;
    pMech->iFacing = mecha_angle_wrap(pMech->iFacing + iManual);
  }
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

  pMech->iStateTicks++;

  /* --- state selection ------------------------------------------------- */

  if (!mecha_mech_alive(pMech)) {
    pMech->fVelX = 0.0f;
    pMech->fVelZ = 0.0f;
  } else if (pMech->byMove == MECHA_MOVE_DOWN) {
    if (pMech->iStunTicks <= 0) {
      pMech->byMove = MECHA_MOVE_RISE;
      pMech->iStateTicks = 0;
      pMech->iStunTicks = MECHA_RISE_TICKS;
      pMech->iInvulnTicks = MECHA_RISE_INVULN;
    }
  } else if (pMech->byMove == MECHA_MOVE_RISE) {
    if (pMech->iStunTicks <= 0) {
      pMech->byMove = MECHA_MOVE_STAND;
      pMech->iStateTicks = 0;
    }
  } else if (pMech->byMove == MECHA_MOVE_STAGGER) {
    if (pMech->iStunTicks <= 0) {
      pMech->byMove = bAirborne ? MECHA_MOVE_JUMP : MECHA_MOVE_STAND;
      pMech->iStateTicks = 0;
    }
  } else if (pMech->byMove == MECHA_MOVE_LAND) {
    if (pMech->iStateTicks >= pDef->iLandTicks) {
      pMech->byMove = MECHA_MOVE_STAND;
      pMech->iStateTicks = 0;
    }
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
    } else if (pMech->byMove != MECHA_MOVE_JUMP
               && pMech->byMove != MECHA_MOVE_CANCEL) {
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
               && pInput->bDash
               && pMech->iStateTicks < pDef->iDashTicks
               && mecha_boost_available(pMech)) {
      /* Holding the button keeps the burst going until either the timer or
       * the gauge runs out. */
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

  /* Ending a dash: the burst decides when, not the player. */
  if (pMech->byMove == MECHA_MOVE_DASH
      && (pMech->iStateTicks >= pDef->iDashTicks
          || !mecha_boost_available(pMech))) {
    pMech->byMove = MECHA_MOVE_STAND;
    pMech->iStateTicks = 0;
  }

  /* --- boost recovery --------------------------------------------------- */

  if (pMech->byMove != MECHA_MOVE_DASH && !bBoosting) {
    if (pMech->byMove == MECHA_MOVE_GUARD)
      mecha_regain_boost(pMech, pDef->iBoostGuardRegen);
    else if (pMech->byMove != MECHA_MOVE_JUMP)
      mecha_regain_boost(pMech, pDef->iBoostRegen);
  }

  /* --- integration ------------------------------------------------------ */

  if (bAirborne || pMech->byMove == MECHA_MOVE_JUMP
      || pMech->byMove == MECHA_MOVE_CANCEL) {
    float fGravity = MECHA_GRAVITY;

    if (bBoosting)
      fGravity *= 0.18f;
    pMech->fVelY -= fGravity * MECHA_DT;
  } else {
    pMech->fVelY = 0.0f;
  }

  pMech->fX += pMech->fVelX * MECHA_DT;
  pMech->fZ += pMech->fVelZ * MECHA_DT;
  pMech->fY += pMech->fVelY * MECHA_DT;

  mecha_arena_resolve_cylinder(&pWorld->arena, pDef->fRadius, pMech->fY,
                               pDef->fHeight, &pMech->fX, &pMech->fZ);

  fGround = mecha_arena_ground_height(&pWorld->arena, pMech->fX, pMech->fZ,
                                      pMech->fY);
  if (pMech->fY <= fGround) {
    bool bWasFalling = pMech->fVelY < 0.0f;

    pMech->fY = fGround;
    pMech->fVelY = 0.0f;
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

  /* --- cosmetic smoothing ---------------------------------------------- */

  {
    float fSpeed = mecha_length2(pMech->fVelX, pMech->fVelZ);
    float fLean = 0.0f;

    if (pMech->byMove == MECHA_MOVE_DASH)
      fLean = (float)MECHA_DEG(9);
    else if (pMech->byMove == MECHA_MOVE_WALK)
      fLean = (float)MECHA_DEG(3);
    pMech->fLeanRoll = mecha_approachf(pMech->fLeanRoll, fLean,
                                       (float)MECHA_DEG(40) * MECHA_DT);
    pMech->fStepPhase += fSpeed * MECHA_DT / (2.0f * MECHA_METRE);
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
    if (fSpeed > MECHA_LEG_WALK_SPEED) {
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

static void mecha_projectile_detonate(tMechaWorld *pWorld,
                                      tMechaProjectile *pShot,
                                      int iDirectVictim)
{
  if (pShot->fBlastRadius > 0.0f) {
    mecha_sim_explode(pWorld, (int)pShot->byOwner, pShot->fX, pShot->fY,
                      pShot->fZ, pShot->fBlastRadius, pShot->fDamage,
                      pShot->fStagger, pShot->byPalette);
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
  pMech->fStepPhase = 0.0f;
  pMech->iLegYaw = pMech->iFacing;
  pMech->bLegsBackward = false;
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

  if (pWorld->match.iRoundTicks <= 0) {
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
    if (pMatch->iRoundTicks > 0)
      pMatch->iRoundTicks--;
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
  pWorld->match.iRoundTimeLimit = MECHA_SEC(90.0f);
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
  mecha_update_effects(pWorld);
  mecha_resolve_overlaps(pWorld);

  if (pWorld->match.byPhase == MECHA_PHASE_FIGHT)
    mecha_check_round_end(pWorld);
}
