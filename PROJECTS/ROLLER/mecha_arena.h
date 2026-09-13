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

/* True when the point is inside the arena's playable area, ignoring boxes.
 * Square, octagon or open platform, according to the arena's shape. */
bool mecha_arena_contains(const tMechaArena *pArena, float fX, float fZ);

/*
 * The surface word under a point: the engine's own bits, as stored per
 * terrain cell. Zero off the grid and for a level arena, which is the same
 * thing as "ordinary ground you cannot leave".
 */
uint32_t mecha_arena_surface(const tMechaArena *pArena, float fX, float fZ);

/* The ground alone, with nothing standing on it: what the floor mesh is
 * drawn from, and what a slope's steepness is measured against. */
float mecha_arena_terrain_height(const tMechaArena *pArena, float fX,
                                 float fZ);

/* Just the tabletop's contribution, for anything that needs to know whether
 * a point is on it rather than how high the ground is there. */
float mecha_arena_mesa_height(const tMechaArena *pArena, float fX, float fZ);

//-------------------------------------------------------------------------------------------------
/*
 * How much of its grip a machine keeps on this ground, 0 to 1.
 *
 * Takes a position because grip is a property of the surface and the race
 * game varies it across a track; nothing here does yet, so every point in
 * an arena answers the same. One is the best surface the race game has,
 * and is what an arena gets unless it asks for worse.
 */
float mecha_arena_grip(const tMechaArena *pArena, float fX, float fZ);

/* The race game's own fourteen grades, 0 being the best. Out-of-range
 * levels clamp rather than reading off the end of the table. */
#define MECHA_GRIP_LEVELS 14
float mecha_arena_grip_level(int iLevel);

/*
 * Where there is no floor at all -- off the edge of an open arena. Far
 * enough down that nothing lands on it and gravity has time to do its work
 * before the kill plane does.
 */
#define MECHA_ARENA_VOID (-4000.0f * MECHA_METRE)

/*
 * The octagon, shared between the boundary test and the mesh so the wall
 * stands exactly where the collision says it does. Cut c off each end of a
 * side of length 2h; the cut is c*sqrt(2) long, and for eight equal sides
 * c = 2h/(2 + sqrt(2)). Along a cut |x| + |z| = 2h - c, which is h*sqrt(2).
 */
#define MECHA_OCTAGON_ROOT2 1.41421356f

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

/*
 * Where to head next to get from (fX,fZ) towards (fToX,fToZ) along one of
 * the arena's ways, or false when there is no way worth joining -- no ways
 * at all, none within reach, or one that leads the wrong way. fLook is how
 * far ahead along the way to aim, and iLine picks one of the four lines
 * across it. Writes the aim point in world terms. [AI-13]
 */
bool mecha_arena_way_aim(const tMechaArena *pArena, float fX, float fZ,
                         float fToX, float fToZ, float fLook, int iLine,
                         float *pfAimX, float *pfAimZ);

//-------------------------------------------------------------------------------------------------
/* The tallest lip a walking mech steps straight up onto. */
#define MECHA_ARENA_STEP_UP (1.5f * MECHA_METRE)
//-------------------------------------------------------------------------------------------------
#endif
