#ifndef _ROLLER_MECHA_AI_H
#define _ROLLER_MECHA_AI_H
//-------------------------------------------------------------------------------------------------
/*
 * The computer pilot. It produces exactly the tMechaInput struct a pad
 * produces and nothing else, so an AI mech and a player mech run down the
 * same simulation path and neither can do anything the other cannot.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

/* Fills *pOut with this tick's intent for mech iMechIdx. Reads pWorld, and
 * draws from pWorld->rng, so the AI stays part of the deterministic tick. */
void mecha_ai_think(tMechaWorld *pWorld, int iMechIdx, tMechaInput *pOut);

//-------------------------------------------------------------------------------------------------
#endif
