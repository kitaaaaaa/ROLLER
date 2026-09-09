#ifndef _ROLLER_MECHA_SIM_H
#define _ROLLER_MECHA_SIM_H
//-------------------------------------------------------------------------------------------------
/*
 * The arena simulation: one fixed 60 Hz tick, driven entirely by the world
 * state plus one input struct per mech.
 *
 * There is no SDL and no ROLLER global anywhere behind this header, which is
 * what lets mecha_sim_test.c run whole matches headless and assert on them.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

/* Boost is kept pre-multiplied by the tick rate; see tMechaMech::iBoost. */
#define MECHA_BOOST_SCALE MECHA_TICK_HZ

//-------------------------------------------------------------------------------------------------
/* Setup */

/* Resets pWorld, loads arena iArenaIdx and seeds the RNG. iRoundsToWin below
 * one is treated as one. */
void mecha_sim_init(tMechaWorld *pWorld, int iArenaIdx, uint32_t uiSeed,
                    int iRoundsToWin);

/* Adds a mech and returns its index, or -1 when the world is full. The mech
 * is not placed until mecha_sim_begin_match. */
int mecha_sim_add_mech(tMechaWorld *pWorld, int iDefIdx,
                       uint8_t byController, uint8_t byTeam);

/* Places everyone on the spawn ring and starts round one. */
void mecha_sim_begin_match(tMechaWorld *pWorld);

/*
 * How hard the computer pilots play, as an eMechaAiSkill. Out-of-range
 * values clamp onto the ladder. Call it after mecha_sim_init, which starts
 * every world on MECHA_AI_VETERAN; it applies to every AI-controlled mech
 * in the world and is part of the world state, so a match still replays
 * exactly from its seed.
 */
void mecha_sim_set_ai_skill(tMechaWorld *pWorld, int iSkill);

/* Display name for a skill level. Never NULL. */
const char *mecha_sim_ai_skill_name(int iSkill);

//-------------------------------------------------------------------------------------------------
/* Simulation */

/* Advances one tick. paInputs is indexed by mech index; entries for AI mechs
 * are ignored, and a NULL array (or one shorter than the mech list) reads as
 * no input at all for the mechs it does not cover. */
void mecha_sim_tick(tMechaWorld *pWorld, const tMechaInput *paInputs,
                    int iInputCount);

//-------------------------------------------------------------------------------------------------
/* Queries. Everything here is read-only and safe on a NULL or empty world. */

bool mecha_mech_alive(const tMechaMech *pMech);
/* Which stance the mech's weapons fire from right now. */
eMechaStance mecha_mech_stance(const tMechaMech *pMech);
/* The weapon slot iSlot would fire this instant, or NULL if out of range. */
const tMechaWeaponDef *mecha_mech_weapon(const tMechaWorld *pWorld,
                                         int iMechIdx, int iSlot);
/* 0..1 for the HUD gauge. */
float mecha_mech_boost_fraction(const tMechaWorld *pWorld, int iMechIdx);
float mecha_mech_armour_fraction(const tMechaWorld *pWorld, int iMechIdx);
/* Height of the mech's centre of mass above the floor, for camera aim and
 * for projectile targeting. */
float mecha_mech_centre_height(const tMechaWorld *pWorld, int iMechIdx);
/* Ground directly under the mech: zero on the open floor, a box top when it
 * is standing on one. */
float mecha_mech_ground_height(const tMechaWorld *pWorld, int iMechIdx);
bool mecha_mech_is_airborne(const tMechaWorld *pWorld, int iMechIdx);

/* First mech under human control, or -1. */
int mecha_sim_human_index(const tMechaWorld *pWorld);
/* Nearest live mech on another team, or -1. */
int mecha_sim_nearest_enemy(const tMechaWorld *pWorld, int iMechIdx);

//-------------------------------------------------------------------------------------------------
/* Used by mecha_ai.c, and useful to tests. */

/* Applies damage and stagger from iAttackerIdx (-1 for the arena itself),
 * with knockback along (fPushX, fPushZ). Ignores invulnerable and dead
 * targets. */
void mecha_sim_damage(tMechaWorld *pWorld, int iVictimIdx, int iAttackerIdx,
                      float fDamage, float fStagger,
                      float fPushX, float fPushZ);

void mecha_sim_spawn_effect(tMechaWorld *pWorld, uint8_t byKind,
                            float fX, float fY, float fZ,
                            float fScale, uint8_t byPalette, int iLife);

//-------------------------------------------------------------------------------------------------
#endif
