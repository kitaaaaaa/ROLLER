#include "mecha_math.h"

#include <math.h>

//-------------------------------------------------------------------------------------------------
/*
 * One shared quarter-turn table. Sine and cosine both read it, so the two are
 * exactly consistent -- mecha_cos(a) and mecha_sin(a + 4096) return the same
 * float rather than two separately rounded ones, which keeps headings that
 * are supposed to be perpendicular actually perpendicular after a few
 * thousand ticks of integration.
 *
 * The table is built once from libm. That makes a match deterministic for a
 * given build: same seed and same inputs replay identically, which is what
 * the regression tests rely on. It is not a bit-exact guarantee across
 * different libm implementations, and nothing in the arena depends on one.
 */
#define MECHA_TRIG_QUARTER MECHA_ANGLE_QUARTER

static float s_afQuarterSin[MECHA_TRIG_QUARTER + 1];
static bool s_bTrigReady = false;

static void mecha_trig_init(void)
{
  int i;

  if (s_bTrigReady)
    return;

  for (i = 0; i <= MECHA_TRIG_QUARTER; i++) {
    double dRadians = (double)i * (2.0 * 3.14159265358979323846)
                    / (double)MECHA_ANGLE_FULL;
    s_afQuarterSin[i] = (float)sin(dRadians);
  }
  s_bTrigReady = true;
}

//-------------------------------------------------------------------------------------------------

int mecha_angle_wrap(int iAngle)
{
  return iAngle & MECHA_ANGLE_MASK;
}

//-------------------------------------------------------------------------------------------------

int mecha_angle_delta(int iFrom, int iTo)
{
  int iDelta = (iTo - iFrom) & MECHA_ANGLE_MASK;

  if (iDelta >= MECHA_ANGLE_HALF)
    iDelta -= MECHA_ANGLE_FULL;
  return iDelta;
}

//-------------------------------------------------------------------------------------------------

int mecha_angle_approach(int iFrom, int iTo, int iMaxStep)
{
  int iDelta;

  if (iMaxStep <= 0)
    return mecha_angle_wrap(iFrom);

  iDelta = mecha_angle_delta(iFrom, iTo);
  if (iDelta > iMaxStep)
    iDelta = iMaxStep;
  else if (iDelta < -iMaxStep)
    iDelta = -iMaxStep;
  return mecha_angle_wrap(iFrom + iDelta);
}

//-------------------------------------------------------------------------------------------------

float mecha_sin(int iAngle)
{
  int iWrapped;

  mecha_trig_init();
  iWrapped = iAngle & MECHA_ANGLE_MASK;

  if (iWrapped <= MECHA_ANGLE_QUARTER)
    return s_afQuarterSin[iWrapped];
  if (iWrapped <= MECHA_ANGLE_HALF)
    return s_afQuarterSin[MECHA_ANGLE_HALF - iWrapped];
  if (iWrapped <= MECHA_ANGLE_HALF + MECHA_ANGLE_QUARTER)
    return -s_afQuarterSin[iWrapped - MECHA_ANGLE_HALF];
  return -s_afQuarterSin[MECHA_ANGLE_FULL - iWrapped];
}

//-------------------------------------------------------------------------------------------------

float mecha_cos(int iAngle)
{
  return mecha_sin(iAngle + MECHA_ANGLE_QUARTER);
}

//-------------------------------------------------------------------------------------------------

int mecha_atan2_angle(float fX, float fZ)
{
  double dRadians;
  int iAngle;

  if (fX == 0.0f && fZ == 0.0f)
    return 0;

  /* atan2(x, z) rather than the usual atan2(y, x): the arena measures
   * headings from +Z towards +X, which is the same circle the engine's own
   * car headings use. */
  dRadians = atan2((double)fX, (double)fZ);
  iAngle = (int)lround(dRadians * (double)MECHA_ANGLE_FULL
                     / (2.0 * 3.14159265358979323846));
  return mecha_angle_wrap(iAngle);
}

//-------------------------------------------------------------------------------------------------

float mecha_clampf(float fValue, float fLow, float fHigh)
{
  if (fValue < fLow)
    return fLow;
  if (fValue > fHigh)
    return fHigh;
  return fValue;
}

//-------------------------------------------------------------------------------------------------

int mecha_clampi(int iValue, int iLow, int iHigh)
{
  if (iValue < iLow)
    return iLow;
  if (iValue > iHigh)
    return iHigh;
  return iValue;
}

//-------------------------------------------------------------------------------------------------

float mecha_approachf(float fValue, float fTarget, float fMaxStep)
{
  float fDelta = fTarget - fValue;

  if (fMaxStep <= 0.0f)
    return fValue;
  if (fDelta > fMaxStep)
    return fValue + fMaxStep;
  if (fDelta < -fMaxStep)
    return fValue - fMaxStep;
  return fTarget;
}

//-------------------------------------------------------------------------------------------------

int mecha_stepi(int iValue, int iTarget, int iMaxStep)
{
  if (iMaxStep < 0)
    iMaxStep = -iMaxStep;
  if (iValue < iTarget)
    return iValue + iMaxStep > iTarget ? iTarget : iValue + iMaxStep;
  if (iValue > iTarget)
    return iValue - iMaxStep < iTarget ? iTarget : iValue - iMaxStep;
  return iValue;
}

//-------------------------------------------------------------------------------------------------

float mecha_length2(float fX, float fZ)
{
  return sqrtf(fX * fX + fZ * fZ);
}

//-------------------------------------------------------------------------------------------------

float mecha_length3(float fX, float fY, float fZ)
{
  return sqrtf(fX * fX + fY * fY + fZ * fZ);
}

//-------------------------------------------------------------------------------------------------

void mecha_rng_seed(tMechaRng *pRng, uint32_t uiSeed)
{
  if (!pRng)
    return;
  /* xorshift is stuck at zero, so a zero seed picks up the usual constant. */
  pRng->uiState = uiSeed ? uiSeed : 0x2545F491u;
}

//-------------------------------------------------------------------------------------------------

uint32_t mecha_rng_next(tMechaRng *pRng)
{
  uint32_t uiState;

  if (!pRng)
    return 0u;

  uiState = pRng->uiState;
  if (uiState == 0u)
    uiState = 0x2545F491u;
  uiState ^= uiState << 13;
  uiState ^= uiState >> 17;
  uiState ^= uiState << 5;
  pRng->uiState = uiState;
  return uiState;
}

//-------------------------------------------------------------------------------------------------

int mecha_rng_range(tMechaRng *pRng, int iBound)
{
  if (iBound <= 0)
    return 0;
  return (int)(mecha_rng_next(pRng) % (uint32_t)iBound);
}

//-------------------------------------------------------------------------------------------------

float mecha_rng_unit(tMechaRng *pRng)
{
  /* 24 bits is every value a float can hold exactly below 1.0. */
  return (float)(mecha_rng_next(pRng) >> 8) / 16777216.0f;
}
