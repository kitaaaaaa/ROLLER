#include "mecha_ai.h"

#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_sim.h"

#include <math.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------
/*
 * The computer pilot.
 *
 * It holds no state of its own. Everything it decides comes out of the world
 * it is handed plus the shared RNG, which keeps the whole tick deterministic
 * and means a recorded match replays exactly. Where it needs something that
 * looks like memory -- which way it is currently circling, say -- it derives
 * it from the tick counter instead of storing it.
 */
//-------------------------------------------------------------------------------------------------

/* How long the pilot commits to a strafe direction before reconsidering. */
#define MECHA_AI_STRAFE_TICKS 48

#define MECHA_AI_DODGE_LOOKAHEAD 0.9f

/*
 * A shot this close to passing through us is worth spending boost to avoid.
 *
 * The same at every skill level, and deliberately so: scaling it with skill
 * was tried and made the ladder run backwards. A pilot that dodges more
 * dashes more, a dash changes its stance and swings it off the firing cone,
 * and the boost it burns eventually locks out -- at which point it cannot
 * dodge at all. Measured over five duels the wide-margin pilot both dealt
 * less and absorbed more than the middle rung. Reaction time is the honest
 * lever here; how near a miss has to be to be worth answering is not.
 */
#define MECHA_AI_DODGE_MARGIN (4.0f * MECHA_METRE)

//-------------------------------------------------------------------------------------------------
/*
 * What separates the skill levels.
 *
 * The pilot reads the same world struct the simulation ticks, so it cannot
 * be made worse by hiding things from it -- only by putting human limits
 * back in. The numbers below were picked by measuring, not by taste, and
 * what the measurements say is worth recording because it is not what the
 * obvious design would predict.
 *
 * iAimError is the lever that works. Every weapon aims itself at whatever is
 * locked, so with no error term the pilot fires a perfect solution every
 * time; adding one makes it miss, and over twelve duels the damage it lands
 * falls off cleanly once the error clears the target's own width -- about
 * 23400 at zero, 19100 at nine degrees, 15500 at fourteen. Below roughly
 * four degrees nothing happens at all: the shot radius and the target radius
 * swallow the error. Past about fourteen the curve flattens again.
 *
 * iReactionTicks is not a strength lever, however much it looks like one.
 * Sweeping it from zero to six tenths of a second moved the totals around
 * inside the run-to-run variance and never in a consistent direction: a
 * pilot that answers every shot the instant it is fired also dashes
 * constantly, and dashing swings it off its own firing cone and drains the
 * boost it needs to dodge with. It is kept because it changes how the pilot
 * reads -- a rookie visibly flinches late -- not because it is what makes
 * one harder to beat than another.
 *
 * iTriggerOdds barely touches the damage the pilot deals, but hesitating
 * measurably raises what it absorbs, which is the half a losing player
 * actually feels.
 */
typedef struct
{
  int iReactionTicks;  /* a shot is invisible to the pilot until this old */
  int iAimError;       /* peak error either side of the firing solution */
  int iTriggerOdds;    /* 1-in-N per tick of committing to a shot */
} tMechaAiProfile;

static const tMechaAiProfile s_aAiProfiles[MECHA_AI_SKILL_COUNT] = {
  [MECHA_AI_ROOKIE]  = { MECHA_SEC(0.30f), MECHA_DEG(14), 8 },
  [MECHA_AI_VETERAN] = { MECHA_SEC(0.20f), MECHA_DEG(9),  3 },
  [MECHA_AI_ACE]     = { MECHA_SEC(0.15f), 0,             1 },
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
  float fBestScore = 0.0f;
  int iBest = -1;
  int iSlot;

  for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
    const tMechaWeaponDef *pWeapon = &pDef->aWeapons[iSlot][eStance];
    float fWant;
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

    /* Damage per second, discounted by how far the shot is from the range it
     * wants to be taken at. */
    fWant = mecha_ai_weapon_range(pWeapon);
    fScore = pWeapon->fDamage * (float)pWeapon->byCount
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
   * sit down and refill, which is what crouching is for. */
  if ((!bAmmoLeft || (!bHasLine && fBoost < 0.35f))
      && fDistance > fPreferred * 0.8f) {
    pOut->bCrouch = true;
    pOut->iMoveX = 0;
    pOut->iMoveZ = 0;
    pOut->bDash = false;
  }

  /* Take the high ground now and then, or hop a wall that is in the way. */
  if (!pOut->bCrouch && fBoost > 0.7f
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
    pOut->bCrouch = false;
  }

  /* --- shooting --------------------------------------------------------- */

  iBearing = mecha_atan2_angle(pTarget->fX - pSelf->fX,
                               pTarget->fZ - pSelf->fZ);
  iOff = mecha_angle_delta(pSelf->iFacing, iBearing);
  if (iOff < 0)
    iOff = -iOff;

  iSlot = mecha_ai_choose_weapon(pWorld, iMechIdx, fDistance, bHasLine);
  if (iSlot >= 0 && pSelf->iRecovery <= 0 && iOff <= MECHA_AI_FIRE_CONE
      && !pSelf->abFireHeld[iSlot]
      /* A moment's hesitation on the shot rather than the trigger coming
       * down the instant the solution is good. */
      && mecha_rng_range(&pWorld->rng, pProfile->iTriggerOdds) == 0) {
    /*
     * How badly this pilot is about to shoot. Rolled once per shot, on the
     * shared RNG so the match still replays from its seed, and read by
     * mecha_sim_fire after it has worked out where the target will be --
     * every weapon aims itself at the lock, so this is the only thing that
     * makes a computer pilot miss.
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
