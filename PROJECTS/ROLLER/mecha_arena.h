#ifndef _ROLLER_MECHA_ARENA_H
#define _ROLLER_MECHA_ARENA_H
//-------------------------------------------------------------------------------------------------
/*
 * Arena geometry and the collision queries the simulation runs against it.
 *
 * The arena is deliberately simple: a square floor, four walls, and a handful
 * of axis-aligned boxes standing on it. That is enough for cover, for height
 * play (the boxes are solid ground you can land on), and for the AI to reason
 * about, while staying cheap enough that the software rasteriser can draw the
 * whole thing back-to-front every frame without a depth buffer.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

int mecha_arena_count(void);
const char *mecha_arena_name(int iArenaIdx);

/* Fills pArena with arena iArenaIdx. Out-of-range indices wrap, so a caller
 * can cycle without bounds-checking. */
void mecha_arena_init(tMechaArena *pArena, int iArenaIdx);

/* Height of solid ground under (fX, fZ) -- zero on the open floor, the top of
 * an obstacle when standing on one. When several boxes overlap the point, the
 * tallest wins.
 *
 * fFeetY is where the mech's feet are now: a box only counts as ground when
 * the feet are at or above its top (within MECHA_ARENA_STEP_UP), so walking
 * into the side of a box is a wall rather than a teleport onto it. */
float mecha_arena_ground_height(const tMechaArena *pArena,
                                float fX, float fZ, float fFeetY);

/* Pushes a standing cylinder out of the walls and out of any box it has
 * driven into. fFeetY and fHeight decide which boxes it can intersect at all.
 * Returns true when anything moved it. */
bool mecha_arena_resolve_cylinder(const tMechaArena *pArena,
                                  float fRadius, float fFeetY, float fHeight,
                                  float *pfX, float *pfZ);

/* True when the point is inside the arena's playable square, ignoring boxes. */
bool mecha_arena_contains(const tMechaArena *pArena, float fX, float fZ);

/* Traces the segment from (fX0,fY0,fZ0) to (fX1,fY1,fZ1) against the floor,
 * the walls and the boxes. On a hit, writes the impact point and returns
 * true; otherwise leaves the outputs alone and returns false. Any of the
 * output pointers may be NULL. */
bool mecha_arena_trace_segment(const tMechaArena *pArena,
                               float fX0, float fY0, float fZ0,
                               float fX1, float fY1, float fZ1,
                               float *pfHitX, float *pfHitY, float *pfHitZ);

/* A spawn ring: iSlot of iCount, evenly spaced and facing the middle. */
void mecha_arena_spawn_point(const tMechaArena *pArena, int iSlot, int iCount,
                             float *pfX, float *pfZ, int *piFacing);

//-------------------------------------------------------------------------------------------------
/* The tallest lip a walking mech steps straight up onto. */
#define MECHA_ARENA_STEP_UP (1.5f * MECHA_METRE)
//-------------------------------------------------------------------------------------------------
#endif
