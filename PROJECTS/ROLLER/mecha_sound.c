//-------------------------------------------------------------------------------------------------
/*
 * The arena's sound, played through Whiplash's mixer. See mecha_sound.h for
 * the shape of it and docs/arena-notes.md [SND-01] for why it reads the
 * world instead of being told about it.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_sound.h"

#include "mecha_defs.h"
#include "mecha_math.h"
#include "mecha_render.h"
#include "mecha_sim.h"

#include "sound.h"

#include <math.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------

/* Which of Whiplash's samples the arena borrows, and what it uses them as. */
#define MECHA_SFX_ENGINE  SOUND_SAMPLE_ENGINE   /* the machine, idling to flat out */
#define MECHA_SFX_SKID    SOUND_SAMPLE_SKID1    /* tyres and feet losing grip */
#define MECHA_SFX_LAND    SOUND_SAMPLE_LANDSKID /* coming down off a jump */
#define MECHA_SFX_BLAST   SOUND_SAMPLE_EXPLO    /* a shell going off */
#define MECHA_SFX_WRECK   SOUND_SAMPLE_BIGCRASH /* a machine going up */
#define MECHA_SFX_HIT     SOUND_SAMPLE_FENDER   /* two machines meeting */

/*
 * Inverse-square with a floor, exactly as enginesound() has it: the constant
 * is the distance squared at which a sound is half as loud, so a machine is
 * down to half volume 32 m away.
 */
#define MECHA_SND_ROLLOFF     65536000.0f
/* Below this the mixer is being asked for silence, so ask for nothing. */
#define MECHA_SND_FLOOR       256
#define MECHA_SND_FULL        0x7FFF
/* Whiplash's own pitch base for a looped engine, and the span an engine
 * covers between idle and flat out. */
#define MECHA_SND_PITCH_BASE  8192.0f
#define MECHA_SND_PITCH_SPAN  100000.0f
/* A walker's servos are a narrower band than an engine: it is a hum that
 * rises as it moves, not a rev range. */
#define MECHA_SND_SERVO_SPAN  34000.0f
/* The skid sample's own base, and the speed that doubles its pitch. */
#define MECHA_SND_SKID_BASE   65536.0f
#define MECHA_SND_SKID_SPEED  6400.0f
/* Sound travels at 343 m/s here as anywhere else. */
#define MECHA_SND_MACH        MECHA_MPS(343.0f)
/* A machine has to be this far off its line before the tyres complain. */
#define MECHA_SND_SLIP_ANGLE  MECHA_DEG(9)
#define MECHA_SND_SLIP_FULL   MECHA_DEG(38)
/* And moving at all, or a machine turning on the spot squeals. */
#define MECHA_SND_SLIP_SPEED  MECHA_MPS(6.0f)
/* How hard a landing has to be to be worth a sample, and what counts as the
 * full-volume one. */
#define MECHA_SND_LAND_SOFT   MECHA_MPS(6.0f)
#define MECHA_SND_LAND_HARD   MECHA_MPS(34.0f)

//-------------------------------------------------------------------------------------------------

/* What the update compares against to spot the things that make a noise. */
static bool  s_abEffectWas[MECHA_MAX_EFFECTS];
static int   s_aiRamWas[MECHA_MAX_MECHS];
static bool  s_abAirborneWas[MECHA_MAX_MECHS];
static float s_afFallWas[MECHA_MAX_MECHS];
static bool  s_abEngineOn[MECHA_MAX_MECHS];
static bool  s_abSkidOn[MECHA_MAX_MECHS];
static bool  s_bActive;

//-------------------------------------------------------------------------------------------------

/* Everything the listener needs to know about one noise in the arena. */
typedef struct
{
  int   iPan;               /* 0 hard left, 0x8000 centre, 0xFFFF hard right */
  float fAttenuation;       /* 1 at the listener, falling off with distance */
  float fDoppler;           /* pitch multiplier from closing speed */
} tMechaSoundPlace;

/*
 * Where a point in the arena sits relative to the listener. The pan and the
 * doppler are worked out the way enginesounds() works them out for a car:
 * pan off the angle between the camera's right and forward axes, doppler off
 * the closing speed along the line between the two. [SND-02]
 */
static void mecha_sound_place(const tMechaCamera *pCamera, float fX, float fY,
                              float fZ, float fVelX, float fVelY, float fVelZ,
                              tMechaSoundPlace *pOut)
{
  float afRight[3];
  float afUp[3];
  float afForward[3];
  float fDx;
  float fDy;
  float fDz;
  float fDistSq;
  float fDist;
  float fSide;
  float fAhead;
  float fClosing;
  double dPan;

  pOut->iPan = 0x8000;
  pOut->fAttenuation = 0.0f;
  pOut->fDoppler = 1.0f;
  if (!pCamera)
    return;

  mecha_camera_basis(pCamera, afRight, afUp, afForward);
  fDx = fX - pCamera->fX;
  fDy = fY - pCamera->fY;
  fDz = fZ - pCamera->fZ;
  fDistSq = fDx * fDx + fDy * fDy + fDz * fDz;
  fDist = sqrtf(fDistSq);
  pOut->fAttenuation = MECHA_SND_ROLLOFF / (fDistSq + MECHA_SND_ROLLOFF);

  fSide = fDx * afRight[0] + fDy * afRight[1] + fDz * afRight[2];
  fAhead = fDx * afForward[0] + fDy * afForward[1] + fDz * afForward[2];
  /*
   * Straight ahead is centre, straight out to one side is hard over, and
   * behind folds back onto the near side -- which is all two speakers can
   * say. Zero is hard left and 0xFFFF hard right: DIGISetPanLocation turns
   * this into iPan / 0x8000 - 1 and hands it to the mixer as -1 left to +1
   * right, which is what decides the sign here. [SND-02]
   */
  dPan = (1.0 + (double)mecha_sin(mecha_atan2_angle(fSide, fAhead)))
         * 32768.0;
  if (dPan < 0.0)
    dPan = 0.0;
  pOut->iPan = dPan >= 65535.0 ? 0xFFFF : (int)dPan;

  /* Closing on the listener raises the pitch, opening away drops it. */
  if (fDist > 1.0f) {
    fClosing = (fVelX * fDx + fVelY * fDy + fVelZ * fDz) / fDist;
    if (fClosing > MECHA_SND_MACH * 0.5f)
      fClosing = MECHA_SND_MACH * 0.5f;
    if (fClosing < -MECHA_SND_MACH * 0.5f)
      fClosing = -MECHA_SND_MACH * 0.5f;
    pOut->fDoppler = MECHA_SND_MACH / (MECHA_SND_MACH - fClosing);
  }
}

//-------------------------------------------------------------------------------------------------

/* A volume that has been through the distance and the player's own setting,
 * or zero when it is not worth the mixer's time. */
static int mecha_sound_volume(float fLevel, const tMechaSoundPlace *pPlace,
                              int iSetting)
{
  float fVolume;

  if (fLevel <= 0.0f)
    return 0;
  if (fLevel > 1.0f)
    fLevel = 1.0f;
  fVolume = fLevel * (float)MECHA_SND_FULL * pPlace->fAttenuation
            * ((float)iSetting / 127.0f);
  if (fVolume < (float)MECHA_SND_FLOOR)
    return 0;
  return fVolume > (float)MECHA_SND_FULL ? MECHA_SND_FULL : (int)fVolume;
}

//-------------------------------------------------------------------------------------------------

/* A one-shot somewhere in the arena. */
static void mecha_sound_shot(int iSample, float fLevel,
                             const tMechaSoundPlace *pPlace)
{
  int iVolume = mecha_sound_volume(fLevel, pPlace, SFXVolume);

  if (iVolume > 0)
    pannedsample(iSample, iVolume, pPlace->iPan);
}

//-------------------------------------------------------------------------------------------------

void mecha_sound_enter(void)
{
  static const int aiSamples[] = {
    MECHA_SFX_ENGINE, MECHA_SFX_SKID, MECHA_SFX_LAND,
    MECHA_SFX_BLAST, MECHA_SFX_WRECK, MECHA_SFX_HIT,
  };
  size_t i;

  memset(s_abEffectWas, 0, sizeof(s_abEffectWas));
  memset(s_aiRamWas, 0, sizeof(s_aiRamWas));
  memset(s_abAirborneWas, 0, sizeof(s_abAirborneWas));
  memset(s_afFallWas, 0, sizeof(s_afFallWas));
  memset(s_abEngineOn, 0, sizeof(s_abEngineOn));
  memset(s_abSkidOn, 0, sizeof(s_abSkidOn));
  s_bActive = true;

  /* The race loads the whole set when it starts; the arena wants six of
   * them, and a sample already in memory is not loaded twice. */
  for (i = 0; i < sizeof(aiSamples) / sizeof(aiSamples[0]); i++)
    if (!SamplePtr[aiSamples[i]])
      loadasample(aiSamples[i]);
  initsounds();
}

//-------------------------------------------------------------------------------------------------

void mecha_sound_exit(void)
{
  int i;

  if (!s_bActive)
    return;
  /* Volume zero is how loopsample is told to stop. */
  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    loopsample(i, MECHA_SFX_ENGINE, 0, 0, 0x8000);
    loopsample(i, MECHA_SFX_SKID, 0, 0, 0x8000);
    s_abEngineOn[i] = false;
    s_abSkidOn[i] = false;
  }
  stopmusic();
  s_bActive = false;
}

//-------------------------------------------------------------------------------------------------

void mecha_sound_briefing(void)
{
  if (s_bActive)
    startmusic(optionssong);
}

//-------------------------------------------------------------------------------------------------

void mecha_sound_match(void)
{
  if (s_bActive)
    stopmusic();
}

//-------------------------------------------------------------------------------------------------

/*
 * One machine's engine and tyres. The pitch is Whiplash's: a base the sample
 * was recorded at, plus a span the machine's own speed rides up, times the
 * doppler shift. A walker gets a narrower span, because servos hum where an
 * engine revs. [SND-03]
 */
static void mecha_sound_machine(const tMechaWorld *pWorld, int iMechIdx,
                                const tMechaCamera *pCamera)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef;
  tMechaSoundPlace place;
  float fSpeed;
  float fTop;
  float fRatio;
  float fLevel;
  int iVolume;
  int iPitch;

  if (!pMech->bActive || !mecha_mech_alive(pMech)) {
    if (s_abEngineOn[iMechIdx]) {
      loopsample(iMechIdx, MECHA_SFX_ENGINE, 0, 0, 0x8000);
      s_abEngineOn[iMechIdx] = false;
    }
    if (s_abSkidOn[iMechIdx]) {
      loopsample(iMechIdx, MECHA_SFX_SKID, 0, 0, 0x8000);
      s_abSkidOn[iMechIdx] = false;
    }
    return;
  }

  pDef = mecha_def_get((int)pMech->byDefIdx);
  mecha_sound_place(pCamera, pMech->fX, mecha_mech_centre_height(pWorld, iMechIdx),
                    pMech->fZ, pMech->fVelX, pMech->fVelY, pMech->fVelZ, &place);

  fSpeed = mecha_length2(pMech->fVelX, pMech->fVelZ);
  fTop = pDef->fWalkSpeed > 1.0f ? pDef->fWalkSpeed : 1.0f;
  fRatio = fSpeed / fTop;
  if (fRatio > 1.6f)
    fRatio = 1.6f;

  /* Idling is still a noise, and a machine on its thrusters is the loudest
   * thing short of one coming apart. */
  fLevel = 0.32f + fRatio * 0.48f;
  if (pMech->byMove == MECHA_MOVE_DASH || pMech->byMove == MECHA_MOVE_JUMP)
    fLevel += 0.20f;
  iVolume = mecha_sound_volume(fLevel, &place, EngineVolume);
  iPitch = (int)((MECHA_SND_PITCH_BASE
                  + fRatio * (pDef->bWheeled ? MECHA_SND_PITCH_SPAN
                                             : MECHA_SND_SERVO_SPAN))
                 * place.fDoppler);
  loopsample(iMechIdx, MECHA_SFX_ENGINE, iVolume, iPitch, place.iPan);
  s_abEngineOn[iMechIdx] = iVolume > 0;

  /*
   * Skid is the mismatch between where a machine points and where it is
   * actually going, which is how Whiplash decides a car is sliding -- it
   * compares the steered yaw against the one the car ended up with.
   */
  fLevel = 0.0f;
  if (fSpeed > MECHA_SND_SLIP_SPEED && !mecha_mech_is_airborne(pWorld, iMechIdx)) {
    int iTravel = mecha_atan2_angle(pMech->fVelX, pMech->fVelZ);
    int iSlip = mecha_angle_delta(pMech->iFacing, iTravel);

    if (iSlip < 0)
      iSlip = -iSlip;
    /* Backwards is not sideways: a machine reversing is travelling a half
     * turn off its nose and is not sliding at all. */
    if (iSlip > MECHA_ANGLE_QUARTER)
      iSlip = MECHA_ANGLE_HALF - iSlip;
    if (iSlip > MECHA_SND_SLIP_ANGLE) {
      fLevel = (float)(iSlip - MECHA_SND_SLIP_ANGLE)
               / (float)(MECHA_SND_SLIP_FULL - MECHA_SND_SLIP_ANGLE);
      if (fLevel > 1.0f)
        fLevel = 1.0f;
      /* Faster slides are louder slides. */
      fLevel *= fRatio > 1.0f ? 1.0f : fRatio;
    }
  }
  iVolume = mecha_sound_volume(fLevel, &place, SFXVolume);
  iPitch = (int)((fSpeed / MECHA_SND_SKID_SPEED + 1.0f) * MECHA_SND_SKID_BASE
                 * place.fDoppler);
  loopsample(iMechIdx, MECHA_SFX_SKID, iVolume, iPitch, place.iPan);
  s_abSkidOn[iMechIdx] = iVolume > 0;
}

//-------------------------------------------------------------------------------------------------

void mecha_sound_update(const tMechaWorld *pWorld, const tMechaCamera *pCamera)
{
  int i;

  if (!s_bActive || !pWorld || !pCamera || !soundon)
    return;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    const tMechaMech *pMech = &pWorld->aMechs[i];
    tMechaSoundPlace place;
    bool bAirborne;

    mecha_sound_machine(pWorld, i, pCamera);
    if (!pMech->bActive)
      continue;

    mecha_sound_place(pCamera, pMech->fX,
                      mecha_mech_centre_height(pWorld, i), pMech->fZ,
                      pMech->fVelX, pMech->fVelY, pMech->fVelZ, &place);

    /* Two machines meeting. The ram cooldown going up is the sim saying one
     * of them just ran into something. */
    if (pMech->iRamCooldown > s_aiRamWas[i])
      mecha_sound_shot(MECHA_SFX_HIT, 0.9f, &place);
    s_aiRamWas[i] = pMech->iRamCooldown;

    /* Coming down. The fall speed is read a frame early because by the time
     * the wheels are on the ground it has already been spent. */
    bAirborne = mecha_mech_is_airborne(pWorld, i);
    if (s_abAirborneWas[i] && !bAirborne) {
      float fFall = -s_afFallWas[i];

      if (fFall > MECHA_SND_LAND_SOFT)
        mecha_sound_shot(MECHA_SFX_LAND,
                         (fFall - MECHA_SND_LAND_SOFT)
                           / (MECHA_SND_LAND_HARD - MECHA_SND_LAND_SOFT),
                         &place);
    }
    s_abAirborneWas[i] = bAirborne;
    s_afFallWas[i] = pMech->fVelY;
  }

  /*
   * Blasts. An effect slot going from empty to full is one that was born
   * this frame, which catches them however many ticks the frame ran.
   */
  for (i = 0; i < MECHA_MAX_EFFECTS; i++) {
    const tMechaEffect *pFx = &pWorld->aEffects[i];
    bool bWas = s_abEffectWas[i];

    s_abEffectWas[i] = pFx->bActive;
    if (!pFx->bActive || bWas || pFx->byKind != MECHA_FX_EXPLOSION)
      continue;

    {
      tMechaSoundPlace place;
      /* Scale is how big the blast is; the biggest of them are a machine
       * coming apart and get the wreck sample instead. */
      bool bWreck = pFx->fScale >= MECHA_M(4.0f);

      mecha_sound_place(pCamera, pFx->fX, pFx->fY, pFx->fZ,
                        pFx->fVelX, pFx->fVelY, pFx->fVelZ, &place);
      mecha_sound_shot(bWreck ? MECHA_SFX_WRECK : MECHA_SFX_BLAST,
                       bWreck ? 1.0f : 0.8f, &place);
    }
  }
}
