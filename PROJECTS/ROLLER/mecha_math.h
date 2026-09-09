#ifndef _ROLLER_MECHA_MATH_H
#define _ROLLER_MECHA_MATH_H
//-------------------------------------------------------------------------------------------------
/*
 * Angle and vector helpers for the mecha arena mode.
 *
 * ROLLER measures every heading on a 14-bit circle (0..16383) and looks the
 * sine and cosine up in tsin[]/tcos[]. Those tables live in 3d.c, which drags
 * in SDL and the whole race state, so the arena simulation keeps its own copy
 * of the same convention: identical angle units, identical wrap, but nothing
 * outside the C standard library behind it. The render layer hands these
 * angles straight to the engine because they mean the same thing there.
 */
//-------------------------------------------------------------------------------------------------
#include <stdbool.h>
#include <stdint.h>
//-------------------------------------------------------------------------------------------------

#define MECHA_ANGLE_FULL    16384
#define MECHA_ANGLE_HALF     8192
#define MECHA_ANGLE_QUARTER  4096
#define MECHA_ANGLE_MASK    0x3FFF

/*
 * The arena keeps ROLLER's world scale so a mech drawn beside a car reads at
 * the right size: a Fatal Racing car is roughly a thousand units long, which
 * puts a metre at a quarter of a thousand.
 */
#define MECHA_METRE 250.0f

//-------------------------------------------------------------------------------------------------

/* Wraps any integer onto the 14-bit circle. Negative inputs wrap the same way
 * the engine's own `& 0x3FFF` does. */
int mecha_angle_wrap(int iAngle);

/* Shortest signed way round from iFrom to iTo, in [-8192, 8191]. */
int mecha_angle_delta(int iFrom, int iTo);

/* iFrom stepped at most iMaxStep units towards iTo. */
int mecha_angle_approach(int iFrom, int iTo, int iMaxStep);

float mecha_sin(int iAngle);
float mecha_cos(int iAngle);

/* Heading of the vector (fX, fZ) on the same circle, with 0 along +Z. */
int mecha_atan2_angle(float fX, float fZ);

//-------------------------------------------------------------------------------------------------

float mecha_clampf(float fValue, float fLow, float fHigh);
int mecha_clampi(int iValue, int iLow, int iHigh);
float mecha_approachf(float fValue, float fTarget, float fMaxStep);

/* Length of (fX, fZ) on the ground plane. */
float mecha_length2(float fX, float fZ);
float mecha_length3(float fX, float fY, float fZ);

//-------------------------------------------------------------------------------------------------
/*
 * Deterministic xorshift. The arena runs a fixed 60 Hz tick and every random
 * draw goes through this, so a match replays identically from the same seed
 * and the AI can be regression-tested.
 */
typedef struct
{
  uint32_t uiState;
} tMechaRng;

void mecha_rng_seed(tMechaRng *pRng, uint32_t uiSeed);
uint32_t mecha_rng_next(tMechaRng *pRng);
/* Uniform in [0, iBound). Returns 0 when iBound <= 0. */
int mecha_rng_range(tMechaRng *pRng, int iBound);
/* Uniform in [0, 1). */
float mecha_rng_unit(tMechaRng *pRng);

//-------------------------------------------------------------------------------------------------
#endif
