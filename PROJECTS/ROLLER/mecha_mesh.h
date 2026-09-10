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
/*
 * Lies flat on a surface and belongs on top of it -- a scorch, a puff of
 * dust. Purely a sorting instruction: it says nothing about how the quad is
 * filled, which is the difference between it and SHADOW. Both are decals as
 * far as the draw order is concerned.
 */
#define MECHA_QUAD_DECAL     0x08

//-------------------------------------------------------------------------------------------------

typedef struct
{
  float   afVert[4][3];   /* world space, wound counter-clockwise seen from the front */
  float   afNormal[3];    /* unit outward normal, for back-face rejection */
  uint8_t byPalette;
  uint8_t byFlags;
  /*
   * Which tile of which of the game's texture banks this quad wants, or
   * MECHA_TEX_NONE for a flat-shaded one. The mesh layer never sees a
   * texture -- it names a bank and a tile and the renderer resolves them,
   * which is what keeps this file free of the engine. Anything that cannot
   * be resolved falls back to byPalette, so the same mesh works with or
   * without the retail data.
   */
  uint8_t byTexBank;
  uint8_t byTile;
} tMechaQuad;

/*
 * The banks the mode draws from. These are the game's own, and the numbers
 * the engine knows them by are not these -- the renderer maps them, because
 * the track bank in particular is bank 0 while its tile count lives at
 * num_textures[19], and that is not a quirk worth spreading.
 */
#define MECHA_TEX_NONE    0
#define MECHA_TEX_EFFECT  1   /* generic/car bank: explosions, fire, smoke */
#define MECHA_TEX_WORLD   2   /* track bank: ground, grass, walls */
#define MECHA_TEX_STRUCT  3   /* building bank: block faces */
/*
 * The effect bank again, recoloured. The game's plasma frames are blue and
 * there is only the one set of them, so every machine's fire came out the
 * same colour and a crossfire was unreadable. The render layer builds these
 * by walking the frames' own indices onto a different ramp of the palette
 * and uploading the result as banks of its own; when it cannot -- no data,
 * no palette -- they fall back to the blue one and nothing breaks.
 */
#define MECHA_TEX_EFFECT_WARM   4
#define MECHA_TEX_EFFECT_VIOLET 5
#define MECHA_TEX_EFFECT_GREEN  6
#define MECHA_TEX_BANK_COUNT    7

/*
 * Frames in the game's generic texture bank. Sequences run start..end
 * inclusive and are walked by an effect's age.
 */
#define MECHA_SPRITE_FIRE_FIRST   4
#define MECHA_SPRITE_FIRE_LAST    7
/*
 * 8..12 are the sky's cloud puffs -- horizon.c picks one of those five for
 * every quad of its dome, and so does this mode. They double as the glow on
 * a plasma bolt, because the bank has no bolt art of its own: 0 and 21..23
 * are smoke, 1..3 the start lights, 4..7 flame and 13..20 the blast. At
 * bolt size a soft blue puff reads as plasma; it is still the same five
 * frames as the sky, which is worth knowing.
 */
#define MECHA_SPRITE_CLOUD_FIRST 8
#define MECHA_SPRITE_CLOUD_LAST 12
#define MECHA_SPRITE_PLASMA_FIRST MECHA_SPRITE_CLOUD_FIRST
#define MECHA_SPRITE_PLASMA_LAST MECHA_SPRITE_CLOUD_LAST
#define MECHA_SPRITE_BLAST_FIRST 13
#define MECHA_SPRITE_BLAST_LAST  20
#define MECHA_SPRITE_SMOKE_FIRST 21
#define MECHA_SPRITE_SMOKE_LAST  23

/* Puffs in a landing's ring of dust. Enough to read as a circle from above
 * and as a spray from the side; many more and a landing is a smoke screen.
 * Out here because the tests count them. */
#define MECHA_DUST_PUFFS 7

/* Puffs on the surface of a bomb's standing fireball. Enough that the edge
 * of it reads as an edge, which is the only thing about it a player has to
 * judge. */
#define MECHA_SHELL_PUFFS 16

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

/* One posed mech. The walk cycle, the dash lean, the guard crouch and the
 * knockdown all come out of the mech's own simulated state. */
void mecha_mesh_mech(tMechaQuadList *pList, const tMechaWorld *pWorld,
                     int iMechIdx);

/* Projectiles and effects are camera-facing, so they need the view heading
 * the renderer is about to draw with. */
/*
 * Told by the render layer whether the game's sprite bank came up. The mesh
 * has no way to ask -- it is libc only, by design -- and it matters here
 * because a keyed frame and the flat square it falls back to want different
 * sizes, and because a glow that is only a glow once it is textured is a
 * blob when it is not. Defaults to false, so a caller that never says
 * still gets geometry that reads.
 */
void mecha_mesh_set_sprites(bool bAvailable);

/*
 * The sky's cloud dome, as arena geometry. Placed around the arena's centre
 * at a radius that dwarfs it, so it reads as distance without ever being
 * reachable, and drawn only when the sprite bank is there to draw it with:
 * a cloud that falls back to a flat square is a grey slab hanging in the
 * air, which is worse than no cloud at all.
 */
void mecha_mesh_clouds(tMechaQuadList *pList, const tMechaWorld *pWorld);

/*
 * Which recoloured copy of the effect bank a shot of this colour should be
 * drawn from. The mesh has the tracer index and nothing else -- it is the
 * one piece of the weapon that reaches the geometry -- so the mapping is by
 * index, and lives next to the roster's own comment about what those
 * indices mean.
 */
int mecha_bolt_bank(uint8_t byPalette);

/*
 * Where a quad belongs in the painter's order: bigger is drawn earlier. The
 * renderer sorts on this and nothing else, so it lives here, with the
 * geometry it describes, and can be measured by the tests rather than
 * inferred from what the screen looks like.
 */
float mecha_quad_depth_key(const tMechaQuad *pQuad, const float afEye[3],
                           const float afForward[3]);

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
