#ifndef _ROLLER_MECHA_MODE_H
#define _ROLLER_MECHA_MODE_H
//-------------------------------------------------------------------------------------------------
/*
 * The arena mode as a frontend state: a 3D mecha duel on ROLLER's software
 * renderer instead of a lap race.
 *
 * The four entry points below are what frontend.c's state table calls. They
 * own the fixed-timestep loop that drives mecha_sim_tick, the camera, and
 * the one scratch buffer the geometry is built into; the mode allocates
 * nothing.
 */
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

/* Picks the match to be played on the next mecha_mode_enter. Out-of-range
 * indices wrap onto the roster and the arena list, and fewer than one round
 * to win is treated as one. Safe to call before the frontend starts. */
void mecha_mode_configure(int iPlayerDef, int iOpponentDef, int iArenaIdx,
                          int iRoundsToWin);

/* Names for a launcher or a select screen; both wrap like the setter. */
const char *mecha_mode_mech_name(int iDefIdx);
const char *mecha_mode_arena_name(int iArenaIdx);
int mecha_mode_mech_count(void);
int mecha_mode_arena_count(void);

//-------------------------------------------------------------------------------------------------
/* Frontend state callbacks. */

void mecha_mode_enter(void);
void mecha_mode_update(void);
void mecha_mode_draw(void);
void mecha_mode_exit(void);

//-------------------------------------------------------------------------------------------------
#endif
