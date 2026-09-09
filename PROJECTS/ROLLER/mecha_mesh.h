#ifndef _ROLLER_MECHA_MESH_H
#define _ROLLER_MECHA_MESH_H
//-------------------------------------------------------------------------------------------------
/*
 * Geometry for the arena mode, built procedurally every frame.
 *
 * ROLLER's cars come out of the retail data files; the mechs cannot, so they
 * are assembled here from boxes instead -- torso, hips, legs, shoulders,
 * arms, head, thrusters -- posed from the simulation's own state. That keeps
 * the mode playable with nothing but the base palette, and it means the walk
 * cycle and the knockdown are driven by the same numbers the physics uses
 * rather than by a canned animation.
 *
 * The output is a flat list of world-space quads with a palette index and a
 * face normal. Nothing here knows about SDL or about the renderer: the
 * caller sorts the list back to front (the software rasteriser has no depth
 * buffer) and submits it. mecha_render.c does that.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

/* Drawn from both sides -- the renderer must not cull it. Billboards and the
 * arena floor use this; solid hull panels do not. */
#define MECHA_QUAD_TWO_SIDED 0x01
/* Self-lit: tracers, thruster plumes, explosions. A renderer that shades by
 * face angle should leave these alone. */
#define MECHA_QUAD_GLOW      0x02
/* Draw through ROLLER's translucent shadow path rather than as solid fill.
 * POLYFLAT already routes SURFACE_FLAG_TRANSPARENT to shadow_poly, so a mech
 * shadow costs nothing extra.
 *
 * A quad carrying this flag must put a SHADE LEVEL in byPalette, not a
 * colour: shadow_poly darkens what is underneath by indexing
 * shade_palette[256 * level], and that table holds only 16 blocks. */
#define MECHA_QUAD_SHADOW    0x04

//-------------------------------------------------------------------------------------------------

typedef struct
{
  float   afVert[4][3];   /* world space, wound counter-clockwise seen from the front */
  float   afNormal[3];    /* unit outward normal, for back-face rejection */
  uint8_t byPalette;
  uint8_t byFlags;
} tMechaQuad;

//-------------------------------------------------------------------------------------------------
/*
 * A caller-owned, fixed-capacity quad buffer. Nothing in the mode allocates,
 * so a frame that would overflow simply stops adding geometry rather than
 * growing or crashing; iDropped records how much was lost so a debug overlay
 * can say so.
 */
typedef struct
{
  tMechaQuad *paQuads;
  int         iCount;
  int         iCapacity;
  int         iDropped;
} tMechaQuadList;

void mecha_quads_reset(tMechaQuadList *pList, tMechaQuad *paStorage,
                       int iCapacity);

/* Appends one quad, deriving its normal from the first three vertices.
 * Returns false when the buffer is full. */
bool mecha_quads_add(tMechaQuadList *pList,
                     const float afVert[4][3],
                     uint8_t byPalette, uint8_t byFlags);

//-------------------------------------------------------------------------------------------------
/* Builders. Each appends to pList and leaves what is already there alone. */

void mecha_mesh_arena(tMechaQuadList *pList, const tMechaArena *pArena);

/* One posed mech. The walk cycle, the dash lean, the crouch and the
 * knockdown all come out of the mech's own simulated state. */
void mecha_mesh_mech(tMechaQuadList *pList, const tMechaWorld *pWorld,
                     int iMechIdx);

/* Projectiles and effects are camera-facing, so they need the view heading
 * the renderer is about to draw with. */
void mecha_mesh_projectiles(tMechaQuadList *pList, const tMechaWorld *pWorld,
                            int iCameraYaw);
void mecha_mesh_effects(tMechaQuadList *pList, const tMechaWorld *pWorld,
                        int iCameraYaw);

/* The flat shadow every mech drops on whatever it is standing over. Cheap,
 * and the only ground contact cue the mode has. */
void mecha_mesh_shadows(tMechaQuadList *pList, const tMechaWorld *pWorld);

//-------------------------------------------------------------------------------------------------
/* Enough for the arena, eight mechs, and a full projectile table. */
#define MECHA_QUAD_CAPACITY 4096
//-------------------------------------------------------------------------------------------------
#endif
