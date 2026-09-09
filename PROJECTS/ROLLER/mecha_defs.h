#ifndef _ROLLER_MECHA_DEFS_H
#define _ROLLER_MECHA_DEFS_H
//-------------------------------------------------------------------------------------------------
/*
 * The mech roster and the balance constants the simulation reads.
 *
 * Every machine here is original to ROLLER. The four are deliberately spread
 * across one axis -- how close you have to get to do damage -- so the same
 * three triggers play differently depending on what you picked.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

/* Metres, metres per second, and degrees, in the units the simulation
 * actually stores. Writing the tables in real-world figures is the only way
 * the balance stays readable. */
#define MECHA_M(x)    ((float)(x) * MECHA_METRE)
#define MECHA_MPS(x)  ((float)(x) * MECHA_METRE)
#define MECHA_DEG(x)  ((int)((float)(x) * (float)MECHA_ANGLE_FULL / 360.0f))
#define MECHA_SEC(x)  ((int)((float)(x) * (float)MECHA_TICK_HZ))

//-------------------------------------------------------------------------------------------------
/* Balance constants shared by every mech. */

#define MECHA_GRAVITY          MECHA_MPS(34.0f)  /* per second, per second */

/* Stagger accumulates from hits and bleeds off; crossing the threshold puts
 * the mech on the floor. Chip damage alone never floors anyone -- it takes a
 * heavy hit or a fast enough chain of light ones. */
#define MECHA_STAGGER_DOWN     100.0f
#define MECHA_STAGGER_DECAY     55.0f            /* per second */

#define MECHA_DOWN_TICKS       MECHA_SEC(1.1f)
#define MECHA_RISE_TICKS       MECHA_SEC(0.45f)
/* Invulnerability covers getting up, so a floored mech is never chained. */
#define MECHA_RISE_INVULN      MECHA_SEC(0.8f)
#define MECHA_STAGGER_TICKS    MECHA_SEC(0.35f)

/* The gauge has to climb back to this fraction of full before dash and jump
 * unlock again after bottoming out. */
#define MECHA_BOOST_UNLOCK_NUM 30
#define MECHA_BOOST_UNLOCK_DEN 100

/* How far off the mech's own heading it may aim vertically. */
#define MECHA_AIM_PITCH_LIMIT  MECHA_DEG(38)

/* Beyond this the lock breaks; the reticle goes cold and homing stops. */
#define MECHA_LOCK_RANGE       MECHA_M(260.0f)

/* Mechs push each other apart rather than overlapping. */
#define MECHA_PUSH_PER_TICK    MECHA_M(0.9f)

//-------------------------------------------------------------------------------------------------

int mecha_def_count(void);
/* Out-of-range indices wrap, so callers can cycle a select screen freely. */
const tMechaMechDef *mecha_def_get(int iDefIdx);

//-------------------------------------------------------------------------------------------------
#endif
