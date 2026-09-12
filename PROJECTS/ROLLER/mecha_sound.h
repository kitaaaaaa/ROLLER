#ifndef _ROLLER_MECHA_SOUND_H
#define _ROLLER_MECHA_SOUND_H
//-------------------------------------------------------------------------------------------------
/*
 * What the arena sounds like, played through Whiplash's own mixer.
 *
 * Everything here reads the world rather than being told about it: the sim
 * has no idea sound exists and stays headless. Engine and skid are loops
 * whose volume, pitch and pan are rewritten every frame the way
 * enginesound() rewrites a car's; blasts, hits and landings are one-shots
 * fired off changes the update can see for itself. [SND-01]
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_render.h"
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

/* Loads the handful of samples the arena borrows and clears the mixer's
 * handle table. Safe to call with no sound card and no sample data: every
 * call below then does nothing. */
void mecha_sound_enter(void);

/* Silences every loop the arena started and hands the mixer back. */
void mecha_sound_exit(void);

/* The briefing has the options music under it; a match has nothing but the
 * machines. */
void mecha_sound_briefing(void);
void mecha_sound_match(void);

/* One frame's worth: loops retuned, one-shots fired. pCamera is where the
 * listener is. Call it after the tick and before the frame is drawn. */
void mecha_sound_update(const tMechaWorld *pWorld, const tMechaCamera *pCamera);

//-------------------------------------------------------------------------------------------------
#endif
