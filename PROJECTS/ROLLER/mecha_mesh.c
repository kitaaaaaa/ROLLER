#include "mecha_mesh.h"

#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_sim.h"

#include "carplans.h"
#include "types.h"

#include <math.h>
#include <string.h>

/*
 * Which of the game's own texture banks are loaded, answered once a frame by
 * the render layer before anything is built. Both start false, so a checkout
 * with no retail data draws the whole mode flat rather than naming tiles no
 * bank has.
 */
static bool s_bSprites = false;
static bool s_bCarSkin = false;

//-------------------------------------------------------------------------------------------------
/* Palette indices used only by the geometry; see mecha_arena.c for the rest. */
#define MECHA_PAL_TRACER_CORE 143
/* Only ever seen if a cloud somehow rasterises flat, which the mesh refuses
 * to let happen -- a pale index so a bug reads as a bug and not as a hole. */
#define MECHA_PAL_CLOUD 143
/* What a puff of dust falls back to if it is ever drawn untextured. */
#define MECHA_PAL_SMOKE 137
/* A billboarded tree is nothing but its sprite, so this is only ever seen
 * where the sprite banks are missing -- and there the trees are not drawn at
 * all. It is the canopy green the built trees use, for the one case where a
 * bank loads but a tile in it does not. */
#define MECHA_PAL_CANOPY 252

/*
 * The gun car. How square to the sky a panel has to look to be painted as
 * the top of the car, how far the gun swings off the nose, how it is held,
 * and what firing does to it.
 */
#define MECHA_ZIZIN_ROOF_FACING 0.55f
#define MECHA_GUN_YAW_LIMIT   MECHA_DEG(40)
#define MECHA_GUN_ROLL        MECHA_DEG(84)
/* How far across the bonnet it points on top of wherever it is aiming. */
#define MECHA_GUN_ACROSS      MECHA_DEG(9)
#define MECHA_GUN_GRIP_RAKE   MECHA_DEG(22)
#define MECHA_GUN_KICK_TICKS  16
#define MECHA_GUN_KICK_PITCH  MECHA_DEG(34)

/*
 * Translucent quads carry a SHADE LEVEL in the low byte, not a colour.
 * POLYFLAT hands SURFACE_FLAG_TRANSPARENT polygons to shadow_poly, which
 * indexes shade_palette[256 * level] to darken whatever is already there --
 * and shade_palette is only 4096 bytes, so the level has to stay under 16 or
 * the read runs off the end of it. The engine's own callers use 2 and 3
 * (func2.c's blankwindow, replay.c's car shadows), so these match.
 */
#define MECHA_SHADE_SHADOW 3
#define MECHA_SHADE_DUST   2

/* The arena floor is a checkerboard rather than one big quad: the software
 * rasteriser has no depth buffer and no texture here, so the tiling is what
 * gives the ground any sense of distance at all. */
/* Across the whole arena, so a 220 metre floor at sixteen was stretching one
 * 64-pixel texture over fourteen metres of ground. Thirty-two puts a tile at
 * roughly the size of a road panel, which is the scale the artwork was drawn
 * at. It costs a thousand quads on a four-thousand budget. */
#define MECHA_FLOOR_TILES 32
/* A bigger arena needs more of them or every tile comes out stretched, but
 * the ground is most of the quad budget, so there is a ceiling. */
#define MECHA_FLOOR_TILES_MAX 48
/* How the ground outside the arena is drawn: rings of the boundary's own
 * shape, each one cut into this many quads a side. It is scenery. */
#define MECHA_OUTER_RINGS 4
#define MECHA_OUTER_SPANS 3

/* Number of ticks a knocked-down mech takes to actually hit the floor. */
#define MECHA_FALL_TICKS 8

//-------------------------------------------------------------------------------------------------

void mecha_quads_reset(tMechaQuadList *pList, tMechaQuad *paStorage,
                       int iCapacity)
{
  if (!pList)
    return;
  pList->paQuads = paStorage;
  pList->iCapacity = paStorage ? iCapacity : 0;
  pList->iCount = 0;
  pList->iDropped = 0;
}

//-------------------------------------------------------------------------------------------------

bool mecha_quads_add(tMechaQuadList *pList, const float afVert[4][3],
                     uint8_t byPalette, uint8_t byFlags)
{
  tMechaQuad *pQuad;
  float afEdge1[3];
  float afEdge2[3];
  float fLength;
  int i;

  if (!pList || !pList->paQuads)
    return false;
  if (pList->iCount >= pList->iCapacity) {
    pList->iDropped++;
    return false;
  }

  pQuad = &pList->paQuads[pList->iCount];
  memcpy(pQuad->afVert, afVert, sizeof(pQuad->afVert));
  pQuad->byPalette = byPalette;
  pQuad->byFlags = byFlags;
  pQuad->byTexBank = MECHA_TEX_NONE;
  pQuad->byTile = 0;

  for (i = 0; i < 3; i++) {
    afEdge1[i] = afVert[1][i] - afVert[0][i];
    afEdge2[i] = afVert[2][i] - afVert[0][i];
  }
  pQuad->afNormal[0] = afEdge1[1] * afEdge2[2] - afEdge1[2] * afEdge2[1];
  pQuad->afNormal[1] = afEdge1[2] * afEdge2[0] - afEdge1[0] * afEdge2[2];
  pQuad->afNormal[2] = afEdge1[0] * afEdge2[1] - afEdge1[1] * afEdge2[0];
  fLength = mecha_length3(pQuad->afNormal[0], pQuad->afNormal[1],
                          pQuad->afNormal[2]);
  if (fLength > 1e-6f) {
    for (i = 0; i < 3; i++)
      pQuad->afNormal[i] /= fLength;
  } else {
    /* Degenerate quad -- keep it drawable but never cullable. */
    pQuad->afNormal[0] = 0.0f;
    pQuad->afNormal[1] = 1.0f;
    pQuad->afNormal[2] = 0.0f;
    pQuad->byFlags |= MECHA_QUAD_TWO_SIDED;
  }

  pList->iCount++;
  return true;
}

//-------------------------------------------------------------------------------------------------
/* Local-space assembly */

typedef struct
{
  float afRot[3][3];   /* local axes expressed in world space, as columns */
  float afOrigin[3];
  float fVerticalScale;
} tMechaPose;

//-------------------------------------------------------------------------------------------------

static void mecha_matrix_multiply(float afOut[3][3], const float afA[3][3],
                                  const float afB[3][3])
{
  float afTemp[3][3];
  int iRow;
  int iCol;

  for (iRow = 0; iRow < 3; iRow++) {
    for (iCol = 0; iCol < 3; iCol++) {
      afTemp[iRow][iCol] = afA[iRow][0] * afB[0][iCol]
                         + afA[iRow][1] * afB[1][iCol]
                         + afA[iRow][2] * afB[2][iCol];
    }
  }
  memcpy(afOut, afTemp, sizeof(afTemp));
}

//-------------------------------------------------------------------------------------------------

/* Yaw about the vertical, then pitch forward, then roll sideways -- applied
 * in that order so a leaning mech that then falls over falls the way it was
 * facing rather than the way it was leaning. */
static void mecha_pose_build(tMechaPose *pPose, int iYaw, int iPitch,
                             int iRoll, float fX, float fY, float fZ,
                             float fVerticalScale)
{
  float fCosY = mecha_cos(iYaw);
  float fSinY = mecha_sin(iYaw);
  float fCosP = mecha_cos(iPitch);
  float fSinP = mecha_sin(iPitch);
  float fCosR = mecha_cos(iRoll);
  float fSinR = mecha_sin(iRoll);
  const float afYawM[3][3] = {
    {  fCosY, 0.0f, fSinY },
    {  0.0f,  1.0f, 0.0f  },
    { -fSinY, 0.0f, fCosY },
  };
  const float afPitchM[3][3] = {
    { 1.0f, 0.0f,   0.0f  },
    { 0.0f, fCosP, -fSinP },
    { 0.0f, fSinP,  fCosP },
  };
  const float afRollM[3][3] = {
    { fCosR, -fSinR, 0.0f },
    { fSinR,  fCosR, 0.0f },
    { 0.0f,   0.0f,  1.0f },
  };

  mecha_matrix_multiply(pPose->afRot, afYawM, afPitchM);
  mecha_matrix_multiply(pPose->afRot, pPose->afRot, afRollM);
  pPose->afOrigin[0] = fX;
  pPose->afOrigin[1] = fY;
  pPose->afOrigin[2] = fZ;
  pPose->fVerticalScale = fVerticalScale;
}

//-------------------------------------------------------------------------------------------------

static void mecha_pose_apply(const tMechaPose *pPose, float fX, float fY,
                             float fZ, float afOut[3])
{
  float fScaledY = fY * pPose->fVerticalScale;

  afOut[0] = pPose->afRot[0][0] * fX + pPose->afRot[0][1] * fScaledY
           + pPose->afRot[0][2] * fZ + pPose->afOrigin[0];
  afOut[1] = pPose->afRot[1][0] * fX + pPose->afRot[1][1] * fScaledY
           + pPose->afRot[1][2] * fZ + pPose->afOrigin[1];
  afOut[2] = pPose->afRot[2][0] * fX + pPose->afRot[2][1] * fScaledY
           + pPose->afRot[2][2] * fZ + pPose->afOrigin[2];
}

//-------------------------------------------------------------------------------------------------

/*
 * A pose hung off another one. The pivot is a point in the parent's space --
 * a hip, a knee, a shoulder -- and the rotation is the parent's with a
 * further turn applied on top, so a forearm swings about an elbow that is
 * itself swinging about a shoulder on a torso that is turning independently
 * of the legs it stands on. Everything the mech is built from is a chain of
 * these; nothing but the root knows where it is in the world.
 */
static void mecha_pose_child(tMechaPose *pOut, const tMechaPose *pParent,
                             float fPivotX, float fPivotY, float fPivotZ,
                             int iYaw, int iPitch, int iRoll)
{
  tMechaPose local;
  float afOrigin[3];

  mecha_pose_apply(pParent, fPivotX, fPivotY, fPivotZ, afOrigin);
  mecha_pose_build(&local, iYaw, iPitch, iRoll, 0.0f, 0.0f, 0.0f, 1.0f);
  mecha_matrix_multiply(pOut->afRot, pParent->afRot, local.afRot);
  pOut->afOrigin[0] = afOrigin[0];
  pOut->afOrigin[1] = afOrigin[1];
  pOut->afOrigin[2] = afOrigin[2];
  pOut->fVerticalScale = pParent->fVerticalScale;
}

//-------------------------------------------------------------------------------------------------
/*
 * Corner numbering packs the three sign bits: bit 0 is +X, bit 1 is +Y,
 * bit 2 is +Z. The face table below is wound so that the normal
 * mecha_quads_add derives from the first three vertices points outward,
 * which is what makes back-face rejection work on a closed box.
 */
static const uint8_t s_aabyBoxFaces[6][4] = {
  { 1, 3, 7, 5 },   /* +X */
  { 0, 4, 6, 2 },   /* -X */
  { 2, 6, 7, 3 },   /* +Y */
  { 0, 1, 5, 4 },   /* -Y */
  { 4, 5, 7, 6 },   /* +Z */
  { 0, 2, 3, 1 },   /* -Z */
};

//-------------------------------------------------------------------------------------------------

static void mecha_add_box(tMechaQuadList *pList, const tMechaPose *pPose,
                          float fCx, float fCy, float fCz,
                          float fHx, float fHy, float fHz,
                          uint8_t byPalette, uint8_t byTopPalette,
                          uint8_t byFlags)
{
  float afCorner[8][3];
  int iCorner;
  int iFace;

  for (iCorner = 0; iCorner < 8; iCorner++) {
    mecha_pose_apply(pPose,
                     fCx + ((iCorner & 1) ? fHx : -fHx),
                     fCy + ((iCorner & 2) ? fHy : -fHy),
                     fCz + ((iCorner & 4) ? fHz : -fHz),
                     afCorner[iCorner]);
  }

  for (iFace = 0; iFace < 6; iFace++) {
    float afVert[4][3];
    int i;

    for (i = 0; i < 4; i++)
      memcpy(afVert[i], afCorner[s_aabyBoxFaces[iFace][i]], sizeof(afVert[i]));
    /* Face 2 is the +Y cap; picking it out is how a hull panel and the plate
     * on top of it get different colours from one call. */
    mecha_quads_add(pList, afVert,
                    iFace == 2 ? byTopPalette : byPalette, byFlags);
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * One tile of ground, with each corner at the height the terrain gives it.
 * A level arena leaves every corner at zero and this is the flat quad it
 * always was; a sloped one gets a quad per tile that follows the hill, and
 * because the four corners need not be coplanar the slope reads as facets
 * rather than as a smooth surface -- which is the look, not a compromise.
 */
static void mecha_add_ground_quad(tMechaQuadList *pList,
                                  const tMechaArena *pArena,
                                  float fX0, float fZ0, float fX1, float fZ1,
                                  uint8_t byPalette)
{
  float afVert[4][3];

  afVert[0][0] = fX0; afVert[0][2] = fZ0;
  afVert[1][0] = fX0; afVert[1][2] = fZ1;
  afVert[2][0] = fX1; afVert[2][2] = fZ1;
  afVert[3][0] = fX1; afVert[3][2] = fZ0;
  afVert[0][1] = mecha_arena_terrain_height(pArena, fX0, fZ0);
  afVert[1][1] = mecha_arena_terrain_height(pArena, fX0, fZ1);
  afVert[2][1] = mecha_arena_terrain_height(pArena, fX1, fZ1);
  afVert[3][1] = mecha_arena_terrain_height(pArena, fX1, fZ0);
  mecha_quads_add(pList, afVert, byPalette, MECHA_QUAD_TWO_SIDED);
}

//-------------------------------------------------------------------------------------------------

/* A vertical panel running from (fX0,fZ0) to (fX1,fZ1). The normal comes out
 * a quarter turn anticlockwise from that direction seen from above, so the
 * caller picks which way the panel faces by choosing which end to start at. */
static void mecha_add_panel(tMechaQuadList *pList,
                            float fX0, float fZ0, float fX1, float fZ1,
                            float fY0, float fY1,
                            uint8_t byPalette, uint8_t byFlags)
{
  float afVert[4][3];

  afVert[0][0] = fX0; afVert[0][1] = fY0; afVert[0][2] = fZ0;
  afVert[1][0] = fX1; afVert[1][1] = fY0; afVert[1][2] = fZ1;
  afVert[2][0] = fX1; afVert[2][1] = fY1; afVert[2][2] = fZ1;
  afVert[3][0] = fX0; afVert[3][1] = fY1; afVert[3][2] = fZ0;
  mecha_quads_add(pList, afVert, byPalette, byFlags);
}

//-------------------------------------------------------------------------------------------------

/* A horizontal quad, normal up. */
static void mecha_add_floor_quad(tMechaQuadList *pList,
                                 float fX0, float fZ0, float fX1, float fZ1,
                                 float fY, uint8_t byPalette, uint8_t byFlags)
{
  float afVert[4][3];

  afVert[0][0] = fX0; afVert[0][1] = fY; afVert[0][2] = fZ0;
  afVert[1][0] = fX0; afVert[1][1] = fY; afVert[1][2] = fZ1;
  afVert[2][0] = fX1; afVert[2][1] = fY; afVert[2][2] = fZ1;
  afVert[3][0] = fX1; afVert[3][1] = fY; afVert[3][2] = fZ0;
  mecha_quads_add(pList, afVert, byPalette, byFlags);
}

//-------------------------------------------------------------------------------------------------

/* Defined with the rest of the quad helpers below, needed by the arena
 * builder above them. */
static void mecha_tag_box(tMechaQuadList *pList, int iFirst, int iBank,
                          int iSide, int iTop);
static void mecha_tag_texture(tMechaQuadList *pList, int iBank, int iTile);

/*
 * A wall, in panels rather than as one slab.
 *
 * The legacy texture path works its coordinates out inside POLYTEX from the
 * tile index and the projected polygon, and it fits exactly one tile to
 * whatever polygon it is given. A wall built as a single quad therefore
 * wears one tile stretched two hundred metres wide and twenty high, which
 * is not a wall with a texture on it, it is a smear. Cut into panels the
 * size of the floor's own tiles, each panel gets a tile at the scale the
 * ground is using and the two agree.
 *
 * Panels are emitted from the start end, so the winding -- and with it
 * which way the wall faces -- is the caller's to pick exactly as before.
 */
static void mecha_add_wall(tMechaQuadList *pList,
                           float fX0, float fZ0, float fX1, float fZ1,
                           float fLowY, float fHighY, float fTile,
                           int iBank, uint8_t byPalette, uint8_t byTile)
{
  float fHeight = fHighY - fLowY;
  float fRunX = fX1 - fX0;
  float fRunZ = fZ1 - fZ0;
  float fLength = mecha_length2(fRunX, fRunZ);
  int iAcross;
  int iUp;
  int iCol;
  int iRow;

  if (fTile < 1.0f || fLength < 1.0f || fHeight < 1.0f)
    return;
  iAcross = (int)(fLength / fTile + 0.5f);
  iUp = (int)(fHeight / fTile + 0.5f);
  if (iAcross < 1)
    iAcross = 1;
  if (iUp < 1)
    iUp = 1;

  for (iRow = 0; iRow < iUp; iRow++) {
    float fBandLow = fLowY + fHeight * (float)iRow / (float)iUp;
    float fBandHigh = fLowY + fHeight * (float)(iRow + 1) / (float)iUp;

    for (iCol = 0; iCol < iAcross; iCol++) {
      float fNear = (float)iCol / (float)iAcross;
      float fFar = (float)(iCol + 1) / (float)iAcross;

      mecha_add_panel(pList, fX0 + fRunX * fNear, fZ0 + fRunZ * fNear,
                      fX0 + fRunX * fFar, fZ0 + fRunZ * fFar,
                      fBandLow, fBandHigh, byPalette, 0);
      mecha_tag_texture(pList, iBank, byTile);
    }
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * A block of cover, in panels for the same reason the walls are. Its sides
 * face outwards, which is the opposite winding to an arena wall: the normal
 * comes a quarter turn anticlockwise from the run seen from above, so each
 * side is walked from the end that puts it on the outside.
 *
 * No underside. It is sitting on the floor and the only way to see one is
 * to get beneath the world.
 */
static void mecha_add_tiled_box(tMechaQuadList *pList, float fX, float fZ,
                                float fHalfX, float fHalfZ, float fBaseY,
                                float fTopY, float fTile, int iBank,
                                uint8_t byPalette, uint8_t byTopPalette,
                                uint8_t byTile, uint8_t byTopTile)
{
  float fLowX = fX - fHalfX;
  float fHighX = fX + fHalfX;
  float fLowZ = fZ - fHalfZ;
  float fHighZ = fZ + fHalfZ;
  int iCols;
  int iRows;
  int iCol;
  int iRow;

  mecha_add_wall(pList, fHighX, fHighZ, fHighX, fLowZ, fBaseY, fTopY, fTile,
                 iBank, byPalette, byTile);
  mecha_add_wall(pList, fLowX, fLowZ, fLowX, fHighZ, fBaseY, fTopY, fTile,
                 iBank, byPalette, byTile);
  mecha_add_wall(pList, fLowX, fHighZ, fHighX, fHighZ, fBaseY, fTopY, fTile,
                 iBank, byPalette, byTile);
  mecha_add_wall(pList, fHighX, fLowZ, fLowX, fLowZ, fBaseY, fTopY, fTile,
                 iBank, byPalette, byTile);

  /* And the roof, tiled the same way, because a wide one stretched its
   * tile exactly as the walls did. */
  iCols = (int)(2.0f * fHalfX / fTile + 0.5f);
  iRows = (int)(2.0f * fHalfZ / fTile + 0.5f);
  if (iCols < 1)
    iCols = 1;
  if (iRows < 1)
    iRows = 1;
  for (iRow = 0; iRow < iRows; iRow++) {
    for (iCol = 0; iCol < iCols; iCol++) {
      mecha_add_floor_quad(pList,
                           fLowX + 2.0f * fHalfX * (float)iCol
                                   / (float)iCols,
                           fLowZ + 2.0f * fHalfZ * (float)iRow
                                   / (float)iRows,
                           fLowX + 2.0f * fHalfX * (float)(iCol + 1)
                                   / (float)iCols,
                           fLowZ + 2.0f * fHalfZ * (float)(iRow + 1)
                                   / (float)iRows,
                           fTopY, byTopPalette, 0);
      mecha_tag_texture(pList, iBank, byTopTile);
    }
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * A corner of the arena's own boundary, wrapping. Eight of them for an
 * octagon and four otherwise -- the same points the walls are built on and
 * the same ones the boundary test uses, which is what lets the ground
 * outside start exactly where the ground inside stops.
 */
static void mecha_boundary_corner(const tMechaArena *pArena, int iCorner,
                                  float *pafOut)
{
  float fExtent = pArena->fHalfExtent;

  if (pArena->byShape == MECHA_ARENA_OCTAGON) {
    float fCut = 2.0f * fExtent / (2.0f + MECHA_OCTAGON_ROOT2);
    float fIn = fExtent - fCut;
    const float aafCorner[8][2] = {
      {  fExtent, -fIn      }, {  fExtent,  fIn      },
      {  fIn,      fExtent  }, { -fIn,      fExtent  },
      { -fExtent,  fIn      }, { -fExtent, -fIn      },
      { -fIn,     -fExtent  }, {  fIn,     -fExtent  },
    };

    pafOut[0] = aafCorner[iCorner & 7][0];
    pafOut[1] = aafCorner[iCorner & 7][1];
    return;
  }
  {
    const float aafCorner[4][2] = {
      {  fExtent, -fExtent }, {  fExtent,  fExtent },
      { -fExtent,  fExtent }, { -fExtent, -fExtent },
    };

    pafOut[0] = aafCorner[iCorner & 3][0];
    pafOut[1] = aafCorner[iCorner & 3][1];
  }
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_arena(tMechaQuadList *pList, const tMechaArena *pArena)
{
  float fExtent;
  float fTile;
  int iTiles;
  int iRow;
  int i;

  if (!pList || !pArena)
    return;

  fExtent = pArena->fHalfExtent;
  iTiles = pArena->iFloorTiles > 0 ? pArena->iFloorTiles : MECHA_FLOOR_TILES;
  if (iTiles > MECHA_FLOOR_TILES_MAX)
    iTiles = MECHA_FLOOR_TILES_MAX;
  fTile = fExtent * 2.0f / (float)iTiles;

  for (iRow = 0; iRow < iTiles; iRow++) {
    int iCol;

    for (iCol = 0; iCol < iTiles; iCol++) {
      float fX0 = -fExtent + fTile * (float)iCol;
      float fZ0 = -fExtent + fTile * (float)iRow;

      bool bAlternate = ((iRow + iCol) & 1) != 0;

      float fMidX = fX0 + fTile * 0.5f;
      float fMidZ = fZ0 + fTile * 0.5f;
      uint32_t uiSurface = mecha_arena_surface(pArena, fMidX, fMidZ);

      /*
       * A pit is a surface that is not drawn -- the flag pair the race game
       * uses for exactly this -- and an arena that is not a square has
       * ground outside itself that nobody should see either.
       */
      if ((uiSurface & MECHA_SURF_SKIP_RENDER) != 0)
        continue;
      if (!mecha_arena_contains(pArena, fMidX, fMidZ))
        continue;

      mecha_add_ground_quad(pList, pArena, fX0, fZ0, fX0 + fTile,
                            fZ0 + fTile,
                            bAlternate ? pArena->byFloorPalette
                                       : pArena->byGridPalette);
      /* The checkerboard survives the texturing: the two tiles alternate
       * the same way the two palette entries do, so a floor with the retail
       * art on it still reads as a grid to move about on rather than as one
       * flat expanse. */
      mecha_tag_texture(pList, MECHA_TEX_WORLD,
                        bAlternate ? pArena->byFloorTile
                                   : pArena->byGridTile);
    }
  }

  /* The four walls, each facing inward. A camera shoved outside the arena
   * sees straight through them rather than at a wall of solid colour,
   * because they are one-sided and get culled from behind. */
  if (pArena->byShape == MECHA_ARENA_OCTAGON) {
    /*
     * Eight sides, walked anticlockwise seen from above so every panel's
     * normal comes out facing the middle. The corner points are the square
     * with its corners cut, which is what the boundary test is too.
     */
    int iSide;

    for (iSide = 0; iSide < 8; iSide++) {
      float afFrom[2];
      float afTo[2];

      mecha_boundary_corner(pArena, iSide, afFrom);
      mecha_boundary_corner(pArena, iSide + 1, afTo);
      mecha_add_wall(pList, afFrom[0], afFrom[1], afTo[0], afTo[1],
                     0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                     pArena->byWallPalette, pArena->byWallTile);
    }
  } else if (pArena->byShape != MECHA_ARENA_OPEN) {
    mecha_add_wall(pList,  fExtent, -fExtent,  fExtent,  fExtent,
                   0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList, -fExtent,  fExtent, -fExtent, -fExtent,
                   0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList,  fExtent,  fExtent, -fExtent,  fExtent,
                   0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList, -fExtent, -fExtent,  fExtent, -fExtent,
                   0.0f, pArena->fWallHeight, fTile, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
  }
  /*
   * An open arena has a lip instead: the platform's own edge, seen from
   * outside as you fall past it. Without it the roof is a paper cutout.
   */
  if (pArena->byShape == MECHA_ARENA_OPEN) {
    float fLip = pArena->fSkirt > 0.0f ? pArena->fSkirt : MECHA_M(6.0f);
    /*
     * Its own panel size, and a coarse one. The skirt is the side of a
     * tower rather than a wall of the arena: it runs far enough down that
     * panelling it at the floor's scale would cost more quads than
     * everything standing on the roof put together, and nothing that far
     * below the player is being looked at closely.
     */
    float fPanel = MECHA_M(20.0f);

    mecha_add_wall(pList,  fExtent,  fExtent,  fExtent, -fExtent,
                   -fLip, 0.0f, fPanel, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList, -fExtent, -fExtent, -fExtent,  fExtent,
                   -fLip, 0.0f, fPanel, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList, -fExtent,  fExtent,  fExtent,  fExtent,
                   -fLip, 0.0f, fPanel, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
    mecha_add_wall(pList,  fExtent, -fExtent, -fExtent, -fExtent,
                   -fLip, 0.0f, fPanel, MECHA_TEX_WORLD,
                   pArena->byWallPalette, pArena->byWallTile);
  }

  /*
   * Ground past the boundary.
   *
   * Nobody can walk on it -- the boundary still stops a machine at the
   * arena's own edge -- and it is drawn coarsely because it is only ever
   * seen at a distance. What it buys is that the arena stops being an
   * island: the forest runs on past where the fight does.
   *
   * It is built as rings of the boundary's own shape rather than as a grid
   * with the middle knocked out, and that is not tidiness. A grid coarse
   * enough to be cheap has tiles far wider than the boundary is straight,
   * so every tile it drops for overlapping the arena takes a wedge of
   * ground with it and the horizon comes out full of holes; every tile it
   * keeps lies coplanar over the arena's own floor. Rings share the edge
   * exactly, so there is neither.
   */
  if (pArena->fOuterReach > fExtent) {
    float fGrow = pArena->fOuterReach / fExtent;
    int iRing;

    for (iRing = 0; iRing < MECHA_OUTER_RINGS; iRing++) {
      /* Each ring a fixed multiple of the last, so they get wider as they
       * get further away and the near ones stay the size of the arena. */
      float fInner = powf(fGrow, (float)iRing / (float)MECHA_OUTER_RINGS);
      float fOuter = powf(fGrow, (float)(iRing + 1)
                                 / (float)MECHA_OUTER_RINGS);
      int iEdge;
      int iEdges = pArena->byShape == MECHA_ARENA_OCTAGON ? 8 : 4;

      for (iEdge = 0; iEdge < iEdges; iEdge++) {
        float afFrom[2];
        float afTo[2];
        int iSpan;

        mecha_boundary_corner(pArena, iEdge, afFrom);
        mecha_boundary_corner(pArena, iEdge + 1, afTo);
        for (iSpan = 0; iSpan < MECHA_OUTER_SPANS; iSpan++) {
          float fA = (float)iSpan / (float)MECHA_OUTER_SPANS;
          float fB = (float)(iSpan + 1) / (float)MECHA_OUTER_SPANS;
          float fAx = afFrom[0] + (afTo[0] - afFrom[0]) * fA;
          float fAz = afFrom[1] + (afTo[1] - afFrom[1]) * fA;
          float fBx = afFrom[0] + (afTo[0] - afFrom[0]) * fB;
          float fBz = afFrom[1] + (afTo[1] - afFrom[1]) * fB;
          float afVert[4][3];
          int iCorner;

          afVert[0][0] = fAx * fInner; afVert[0][2] = fAz * fInner;
          afVert[1][0] = fBx * fInner; afVert[1][2] = fBz * fInner;
          afVert[2][0] = fBx * fOuter; afVert[2][2] = fBz * fOuter;
          afVert[3][0] = fAx * fOuter; afVert[3][2] = fAz * fOuter;
          for (iCorner = 0; iCorner < 4; iCorner++) {
            afVert[iCorner][1] =
                mecha_arena_terrain_height(pArena, afVert[iCorner][0],
                                           afVert[iCorner][2]);
          }
          mecha_quads_add(pList, afVert,
                          ((iRing + iSpan + iEdge) & 1)
                            ? pArena->byFloorPalette : pArena->byGridPalette,
                          MECHA_QUAD_TWO_SIDED);
          mecha_tag_texture(pList, MECHA_TEX_WORLD,
                            ((iRing + iSpan + iEdge) & 1)
                              ? pArena->byFloorTile : pArena->byGridTile);
        }
      }
    }
  }

  for (i = 0; i < pArena->iObstacleCount; i++) {
    const tMechaObstacle *pBox = &pArena->aObstacles[i];
    float fBase = mecha_arena_terrain_height(pArena, pBox->fX, pBox->fZ);

    switch (pBox->byKind) {
    case MECHA_PROP_TREE: {
      /*
       * A trunk with a canopy over it. The canopy overhangs what a machine
       * can walk into, which is right: you can stand under a tree, and the
       * thing you cannot walk through is the trunk.
       */
      float fTrunk = pBox->fHalfX * 0.42f;
      float fCanopy = pBox->fHeight * 0.44f;

      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, fTrunk, fTrunk, fBase,
                          fBase + pBox->fHeight - fCanopy, fTile,
                          MECHA_TEX_WORLD, pBox->byPalette, pBox->byPalette,
                          MECHA_TILE_RUST, MECHA_TILE_RUST);
      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, pBox->fHalfX,
                          pBox->fHalfZ, fBase + pBox->fHeight - fCanopy,
                          fBase + pBox->fHeight, fTile, MECHA_TEX_WORLD,
                          pBox->byTrimPalette, pBox->byTrimPalette,
                          pBox->byTile, pBox->byTopTile);
      /* A second, smaller crown, so a tree is not a lollipop. */
      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, pBox->fHalfX * 0.62f,
                          pBox->fHalfZ * 0.62f, fBase + pBox->fHeight,
                          fBase + pBox->fHeight + fCanopy * 0.55f, fTile,
                          MECHA_TEX_WORLD, pBox->byTrimPalette,
                          pBox->byTrimPalette, pBox->byTile,
                          pBox->byTopTile);
      break;
    }

    case MECHA_PROP_ROCK:
      /* Squat, and stepped, so it reads as stone rather than as a crate. */
      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, pBox->fHalfX,
                          pBox->fHalfZ, fBase - MECHA_M(1.0f),
                          fBase + pBox->fHeight * 0.62f, fTile,
                          MECHA_TEX_WORLD, pBox->byPalette,
                          pBox->byTrimPalette, pBox->byTile,
                          pBox->byTopTile);
      mecha_add_tiled_box(pList, pBox->fX + pBox->fHalfX * 0.18f,
                          pBox->fZ - pBox->fHalfZ * 0.14f,
                          pBox->fHalfX * 0.66f, pBox->fHalfZ * 0.7f,
                          fBase + pBox->fHeight * 0.62f,
                          fBase + pBox->fHeight, fTile, MECHA_TEX_WORLD,
                          pBox->byPalette, pBox->byTrimPalette,
                          pBox->byTile, pBox->byTopTile);
      break;

    default:
      mecha_add_tiled_box(pList, pBox->fX, pBox->fZ, pBox->fHalfX,
                          pBox->fHalfZ, fBase, fBase + pBox->fHeight, fTile,
                          MECHA_TEX_STRUCT, pBox->byPalette,
                          pBox->byTrimPalette, pBox->byTile,
                          pBox->byTopTile);
      break;
    }
  }
}

//-------------------------------------------------------------------------------------------------
/* Mechs */

/* How far the body is tipped over. Falling and getting up are both driven
 * off the simulation's own timers, so the animation can never disagree with
 * when the mech is actually helpless. */
/* One angle towards another, for poses that are held rather than cycled. */
static int mecha_blend_angle(int iFrom, int iTo, float fAmount)
{
  return iFrom + (int)((float)(iTo - iFrom) * mecha_clampf(fAmount, 0.0f,
                                                           1.0f));
}

//-------------------------------------------------------------------------------------------------

/*
 * The walk cycle.
 *
 * fStepPhase counts distance rather than time -- one cycle every five metres
 * -- so a machine that stops mid-stride stops mid-stride, and a heavy one
 * that covers ground slowly takes slow steps without anything having to say
 * so. The thigh swings as a sine of the phase; the knee bends through the
 * forward half of that swing and straightens for the half the foot is on the
 * ground pushing back, which is the difference between walking and a pair of
 * planks pivoting at the hip.
 *
 * Angles are positive forward, and the caller negates them for the pose,
 * because a positive pitch in the pose matrix swings a limb backwards.
 */
#define MECHA_LEG_SWING   MECHA_DEG(41)
#define MECHA_LEG_KNEE    MECHA_DEG(56)
/*
 * Standing, and standing with someone to fight.
 *
 * A machine at ease has its feet apart and its knees off the lock; give it a
 * lock to hold and it settles -- lower, wider, one foot forward -- without
 * any of it being animated as such. The lead angle breathes, which rocks the
 * weight slowly from one foot to the other, and that breath is the only
 * movement in either pose.
 */
#define MECHA_STAND_LEAD   MECHA_DEG(5)
#define MECHA_STAND_KNEE   MECHA_DEG(9)
#define MECHA_STAND_SPLAY  MECHA_DEG(5)
#define MECHA_FIGHT_LEAD   MECHA_DEG(13)
#define MECHA_FIGHT_KNEE   MECHA_DEG(25)
#define MECHA_FIGHT_SPLAY  MECHA_DEG(11)
#define MECHA_STANCE_BREATH MECHA_DEG(3)
/*
 * Boosting on the ground is not running.
 *
 * The thrusters are doing the work, so the legs are not driving the machine
 * anywhere -- they are holding it up and steering it, which is a skater's
 * problem and not a runner's. So: both knees bent through the whole cycle,
 * the weight low, and one leg at a time reaching out to the side and back in
 * a long push while the other glides underneath. The pushing leg straightens
 * as it goes out, exactly as a skater's does, and that is what lets it stay
 * on the floor at full stretch.
 */
#define MECHA_SKATE_KNEE    MECHA_DEG(34)
#define MECHA_SKATE_PUSH    MECHA_DEG(20)
#define MECHA_SKATE_GATHER  MECHA_DEG(11)
#define MECHA_SKATE_EDGE    MECHA_DEG(9)
#define MECHA_SKATE_TICKS   40
/*
 * Hanging. Not a tuck -- a tuck is what you do to clear something -- but a
 * machine with its weight off its feet: one leg reaching a little, the
 * other trailing, both knees soft, and a slow sway so it is not a statue.
 */
#define MECHA_LEG_HANG      MECHA_DEG(14)
#define MECHA_LEG_HANGKNEE  MECHA_DEG(30)
#define MECHA_LEG_TRAIL     MECHA_DEG(22)
#define MECHA_LEG_TRAILKNEE MECHA_DEG(46)
/* A guard is a squat, and a human squat has to be deep to lower anything:
 * the knee travels forward as far as the hip drops, so the two cosines all
 * but cancel until the angles get large. Bird-legged, half of this was
 * enough; on a knee that bends the right way it is not -- and it went
 * deeper again once standing stopped being a machine on locked knees, since
 * a crouch is only a crouch relative to whatever the machine does the rest
 * of the time. */
#define MECHA_LEG_SQUAT   MECHA_DEG(54)
#define MECHA_LEG_SQKNEE  MECHA_DEG(108)

/*
 * What the legs are doing, which is not the same question as what the
 * machine is doing. Walking is a cycle; a stance, a glide, the two
 * airborne shapes and the squat are poses with a little movement in them.
 */
#define MECHA_GAIT_WALK    0
#define MECHA_GAIT_SKATE   1
#define MECHA_GAIT_AIR     2
#define MECHA_GAIT_AIRDASH 3
#define MECHA_GAIT_GUARD   4
#define MECHA_GAIT_STANCE  5

/*
 * A glide runs on its own clock rather than on ground covered.
 *
 * Every other cycle here is paced by distance, which is what makes a heavy
 * machine take slow steps without anything having to say so. A boost breaks
 * that: at seventy metres a second, one stroke every five metres is fourteen
 * cycles a second, and legs moving that fast are a grey blur. So the glide
 * is timed instead -- one long push every two thirds of a second, which is
 * what makes it read as gliding rather than as sprinting.
 */

/* Whether both feet are meant to be on the floor throughout. A stance and a
 * glide are poses the machine holds; a walk is a cycle it steps through, and
 * a leg that never leaves the floor is not a leg that is walking. */
static bool mecha_gait_plants(int iGait)
{
  return iGait == MECHA_GAIT_STANCE || iGait == MECHA_GAIT_SKATE;
}

/*
 * iSide 0 is the left leg. piRoll comes back positive for a hip rolled
 * outwards, which the caller signs for the side it is building.
 */
static void mecha_leg_angles(int iGait, float fPhase, int iSide, int iTick,
                             float fCombat, int *piThigh, int *piKnee,
                             int *piRoll)
{
  int iAngle = (int)(fPhase * (float)MECHA_ANGLE_FULL) & (MECHA_ANGLE_FULL - 1);
  float fCos = mecha_cos(iAngle);
  /* A slow breath for the poses that are not cycles, the two legs half a
   * turn apart so they do not move as one. */
  int iSway = (int)((float)MECHA_DEG(5)
                    * mecha_sin(mecha_angle_wrap(iTick * 90
                                                 + iSide * MECHA_ANGLE_HALF)));

  *piRoll = 0;
  switch (iGait) {
  case MECHA_GAIT_STANCE: {
    /*
     * The lead angle breathes rather than the knees, so the machine rocks
     * its weight between its feet instead of bobbing on the spot, and the
     * left foot is the one that leads.
     */
    float fLead = iSide == 0 ? 1.0f : -1.0f;
    int iBreath = (int)((float)MECHA_STANCE_BREATH
                        * mecha_sin(mecha_angle_wrap(iTick * 70)));

    *piThigh = (int)(fLead * (float)(mecha_blend_angle(MECHA_STAND_LEAD,
                                                       MECHA_FIGHT_LEAD,
                                                       fCombat)
                                     + iBreath));
    *piKnee = mecha_blend_angle(MECHA_STAND_KNEE, MECHA_FIGHT_KNEE, fCombat);
    *piRoll = mecha_blend_angle(MECHA_STAND_SPLAY, MECHA_FIGHT_SPLAY,
                                fCombat);
    return;
  }

  case MECHA_GAIT_SKATE: {
    /*
     * One stroke a cycle, the two legs opposite each other: the pushing leg
     * swings back and straightens while the gliding leg gathers underneath
     * and folds. The push also rolls the hip out; how far it ends up rolled
     * is not decided here, because the foot has to finish on the floor.
     */
    float fPush = mecha_sin(iAngle);
    float fOut = fPush > 0.0f ? fPush : 0.0f;
    float fIn = fPush < 0.0f ? -fPush : 0.0f;

    *piThigh = (int)((float)MECHA_SKATE_GATHER * fIn
                     - (float)MECHA_SKATE_PUSH * fOut);
    *piKnee = (int)((float)MECHA_SKATE_KNEE * (1.0f - 0.6f * fOut)
                    + (float)MECHA_SKATE_KNEE * 0.45f * fIn);
    *piRoll = MECHA_SKATE_EDGE;
    return;
  }

  case MECHA_GAIT_GUARD:
    *piThigh = MECHA_LEG_SQUAT;
    *piKnee = MECHA_LEG_SQKNEE;
    return;

  case MECHA_GAIT_AIR:
    if (iSide == 0) {
      *piThigh = MECHA_LEG_HANG + iSway;
      *piKnee = MECHA_LEG_HANGKNEE;
    } else {
      *piThigh = -MECHA_LEG_TRAIL + iSway;
      *piKnee = MECHA_LEG_TRAILKNEE;
    }
    return;

  case MECHA_GAIT_AIRDASH:
    /*
     * Half of each. The lead leg holds a glide's edge, because the machine
     * is being driven somewhere and is braced against it; the other hangs,
     * because there is nothing under either of them to push against.
     */
    if (iSide == 0) {
      *piThigh = -MECHA_SKATE_PUSH + iSway;
      *piKnee = MECHA_SKATE_KNEE / 2;
      *piRoll = MECHA_SKATE_EDGE * 2;
    } else {
      *piThigh = MECHA_LEG_TRAIL + MECHA_DEG(6) + iSway;
      *piKnee = MECHA_LEG_TRAILKNEE + MECHA_DEG(10);
      *piRoll = MECHA_SKATE_EDGE;
    }
    return;

  default:
    *piThigh = (int)((float)MECHA_LEG_SWING * mecha_sin(iAngle));
    *piKnee = fCos > 0.0f ? (int)((float)MECHA_LEG_KNEE * fCos) : 0;
    return;
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * How far below the hip the ankle ends up, for a given pair of joint angles.
 * This is what keeps the feet on the floor: the body is lowered by whatever
 * the straighter leg has lost, so bending the knees sinks the machine
 * instead of leaving it hanging with its feet in the air.
 *
 * The thigh's pose pitch is -A and the knee's is +K, so the shin's own frame
 * sits at K - A off the vertical and the ankle drops by the cosine of that.
 * Getting this sum wrong is not a small error: it is the difference between
 * a machine that walks and one that skates with its feet through the floor.
 */
static float mecha_leg_reach(int iThigh, int iKnee, float fThighLen,
                             float fShinLen)
{
  return fThighLen * mecha_cos(iThigh)
       + fShinLen * mecha_cos(mecha_angle_wrap(iThigh - iKnee));
}

//-------------------------------------------------------------------------------------------------

/*
 * Where this machine is pointing its weapons, as a heading and an elevation.
 * Under a held lock that is the line to the target, which is what makes the
 * arms track an enemy that is circling you; otherwise it is the machine's
 * own heading and the elevation the sim is carrying, so the arms still point
 * wherever the shot is going to go.
 */
static void mecha_mech_aim(const tMechaWorld *pWorld, int iMechIdx,
                           int *piYaw, int *piPitch)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMech *pTarget;
  const tMechaMechDef *pTargetDef;
  float fDx;
  float fDy;
  float fDz;
  float fFlat;

  *piYaw = pMech->iFacing;
  *piPitch = pMech->iAimPitch >= MECHA_ANGLE_HALF
             ? pMech->iAimPitch - MECHA_ANGLE_FULL : pMech->iAimPitch;

  if (pMech->byLock != MECHA_LOCK_HELD
      || pMech->iTargetIdx < 0 || pMech->iTargetIdx >= MECHA_MAX_MECHS)
    return;
  pTarget = &pWorld->aMechs[pMech->iTargetIdx];
  if (!pTarget->bActive)
    return;
  pTargetDef = mecha_def_get((int)pTarget->byDefIdx);

  fDx = pTarget->fX - pMech->fX;
  fDz = pTarget->fZ - pMech->fZ;
  fDy = (pTarget->fY + 0.62f * pTargetDef->fHeight)
        - (pMech->fY + 0.70f * mecha_def_get((int)pMech->byDefIdx)->fHeight);
  fFlat = mecha_length2(fDx, fDz);
  if (fFlat < 1e-3f)
    return;
  *piYaw = mecha_atan2_angle(fDx, fDz);
  *piPitch = mecha_atan2_angle(fDy, fFlat);
  if (*piPitch >= MECHA_ANGLE_HALF)
    *piPitch -= MECHA_ANGLE_FULL;
}

//-------------------------------------------------------------------------------------------------

/*
 * The drawn attitude, added up.
 *
 * Whiplash keeps the pieces apart all the way to the render pose and sums
 * them in one place at the end (car.c: yaw takes the shake, pitch and roll
 * take the landing wobble, the shake and the control offset). The same
 * three lines are all that is needed here, and keeping them together is
 * what makes it possible to read what a machine's body is doing without
 * chasing the terms round the simulation.
 */
static void mecha_mesh_attitude(const tMechaMech *pMech, int *piYaw,
                                int *piPitch, int *piRoll)
{
  const tMechaAttitude *pAtt = &pMech->attitude;

  *piYaw += pAtt->iYawShake;
  *piPitch += pAtt->iAirPitch + pAtt->iPitchDrive + pAtt->iPitchWobble
              + pAtt->iPitchShake;
  *piRoll += pAtt->iRollSteer + pAtt->iRollWobble + pAtt->iRollShake;
}

//-------------------------------------------------------------------------------------------------

static int mecha_mesh_fall_pitch(const tMechaMech *pMech)
{
  const int iDownPitch = MECHA_DEG(78);

  switch (pMech->byMove) {
  case MECHA_MOVE_DOWN: {
    float fProgress = (float)pMech->iStateTicks / (float)MECHA_FALL_TICKS;

    return (int)((float)iDownPitch * mecha_clampf(fProgress, 0.0f, 1.0f));
  }
  case MECHA_MOVE_RISE: {
    /* iStunTicks runs down through the rise, so it doubles as the
     * animation's own clock. */
    float fProgress = (float)pMech->iStunTicks / (float)MECHA_RISE_TICKS;

    return (int)((float)iDownPitch * mecha_clampf(fProgress, 0.0f, 1.0f));
  }
  case MECHA_MOVE_DESTROYED: return iDownPitch;
  default:                   return 0;
  }
}

//-------------------------------------------------------------------------------------------------

/* The lean, which belongs to the torso alone: a machine leaning into a dash
 * leans over its own legs, and a fall takes the legs with it. */
static int mecha_mesh_lean_pitch(const tMechaMech *pMech)
{
  switch (pMech->byMove) {
  case MECHA_MOVE_DASH:      return MECHA_DEG(12);
  case MECHA_MOVE_JUMP:      return -MECHA_DEG(6);
  /* Nose down through the drop, which is what tells the other player the
   * arc has been thrown away rather than merely peaked. */
  case MECHA_MOVE_CANCEL:    return MECHA_DEG(16);
  case MECHA_MOVE_LAND:      return MECHA_DEG(9);
  case MECHA_MOVE_STAGGER:   return -MECHA_DEG(10);
  default:                   return 0;
  }
}

//-------------------------------------------------------------------------------------------------
/* The one machine on wheels */

/*
 * The ZIZIN KLR 330's body is the race game's own Zizin, polygon for
 * polygon: `xzizin_coords` and `xzizin_pols` out of carplans.c, the same
 * fifty quads the car is drawn with on the track.
 *
 * Two things are converted on the way in. The plan is in the race game's
 * axes -- x along the car, y across it, z up -- where the arena's are x
 * across, y up, z forward, so the three swap and the lateral one is
 * negated. The negation is what keeps the car the right way round: those
 * two frames are of opposite handedness, so swapping the axes alone builds
 * the car's reflection, with its wheel arches, its exhausts and both flanks
 * of its livery on the wrong sides. Reflecting it back reverses every
 * winding the plan had, which is why its panels all face inwards here and
 * why the artwork on them needs a different treatment from the one the rest
 * of the mode gets. And the plan's polygons carry
 * a texture word rather than a colour, indexing a per-car bank this mode
 * does not load, so they are flat-shaded in the machine's own two palette
 * entries instead, picked apart by which way each face looks. The shape is
 * the game's; the paint is the mode's.
 */
#define MECHA_ZIZIN_VERTS 86
/* The size of xzizin_anms in carplans.c, which the header only declares. */
#define MECHA_ZIZIN_ANMS  8

static void mecha_zizin_extent(float *pfLength, float *pfHeight)
{
  float fMinX = xzizin_coords[0].fX;
  float fMaxX = fMinX;
  float fMinZ = xzizin_coords[0].fZ;
  float fMaxZ = fMinZ;
  int i;

  for (i = 1; i < MECHA_ZIZIN_VERTS; i++) {
    if (xzizin_coords[i].fX < fMinX) fMinX = xzizin_coords[i].fX;
    if (xzizin_coords[i].fX > fMaxX) fMaxX = xzizin_coords[i].fX;
    if (xzizin_coords[i].fZ < fMinZ) fMinZ = xzizin_coords[i].fZ;
    if (xzizin_coords[i].fZ > fMaxZ) fMaxZ = xzizin_coords[i].fZ;
  }
  *pfLength = fMaxX - fMinX;
  *pfHeight = fMaxZ - fMinZ;
}

//-------------------------------------------------------------------------------------------------

/*
 * What the plan says a panel is painted with.
 *
 * Three cases, and the race game's own draw path walks all three. Most
 * panels carry a texture word: APPLY_TEXTURE set and the tile in the low
 * byte. Eight of them -- the wheels and the livery -- carry ANMS_LOOKUP
 * instead, which means the low byte is an index into the car's animation
 * table and the real word is a frame out of it; frame zero is the one at
 * rest. The rest carry no texture flag at all and the low byte is a plain
 * palette index, which is how the tyres come out black.
 */
static uint32_t mecha_zizin_surface(int iPoly)
{
  uint32_t uiTex = xzizin_pols[iPoly].uiTex;

  if ((uiTex & CAR_FLAG_ANMS_LOOKUP) != 0) {
    uint32_t uiSlot = uiTex & 0xFFu;

    if (uiSlot >= MECHA_ZIZIN_ANMS)
      return 0u;
    uiTex = xzizin_anms[uiSlot].framesAy[0];
  }
  return uiTex;
}

//-------------------------------------------------------------------------------------------------

static void mecha_add_zizin_body(tMechaQuadList *pList,
                                 const tMechaPose *pPose, float fScale,
                                 float fSink, uint8_t byBody, uint8_t byTop)
{
  int iFirst = pList->iCount;
  int iPoly;
  int i;

  for (iPoly = 0; iPoly < MECHA_ZIZIN_BODY_QUADS; iPoly++) {
    float afVert[4][3];
    int iCorner;

    /*
     * Straight off the plan, with nothing nudged apart. The race game draws
     * this body through a sorted polygon list of its own -- that is what the
     * nNextPolIdx links in the plan are -- and a painter's algorithm has no
     * such list, so two of its panels sharing a plane would have flickered.
     * They do not: the fifty are tested against each other by the coplanar
     * check, and the body is rigid, so passing at one pose is passing at
     * all of them.
     */
    for (iCorner = 0; iCorner < 4; iCorner++) {
      const tVec3 *pPlan = &xzizin_coords[xzizin_pols[iPoly].verts[iCorner]];

      mecha_pose_apply(pPose, -pPlan->fY * fScale,
                       pPlan->fZ * fScale - fSink, pPlan->fX * fScale,
                       afVert[iCorner]);
    }
    if (s_bCarSkin) {
      /*
       * Painted the way the race game paints it: the same file, the same
       * tiles, panel for panel. Nothing is chosen here at all.
       */
      uint32_t uiTex = mecha_zizin_surface(iPoly);

      mecha_quads_add(pList, afVert, (uint8_t)(uiTex & 0xFFu),
                      MECHA_QUAD_TWO_SIDED | MECHA_QUAD_TEX_FLIP);
      if ((uiTex & SURFACE_FLAG_APPLY_TEXTURE) != 0)
        mecha_tag_texture(pList, MECHA_TEX_CAR, (int)(uiTex & 0xFFu));
    } else {
      mecha_quads_add(pList, afVert, byBody, MECHA_QUAD_TWO_SIDED);
    }
  }

  /*
   * With no skin to wear it falls back to the machine's own two colours:
   * bonnet and roof in the lighter, flanks in the darker, picked off each
   * panel's own normal once it has been worked out rather than off where
   * the panel sits -- the plan is a real car body and its sills are as high
   * off the ground as some of its bonnet. Downwards, because reflecting the
   * plan into this frame reversed every winding in it and so every normal:
   * the panel looking at the sky is the one whose normal points at the
   * floor.
   */
  if (!s_bCarSkin) {
    for (i = iFirst; i < pList->iCount; i++) {
      if (pList->paQuads[i].afNormal[1] < -MECHA_ZIZIN_ROOF_FACING)
        pList->paQuads[i].byPalette = byTop;
    }
  }
}

//-------------------------------------------------------------------------------------------------

/*
 * And the gun.
 *
 * It is a handgun, it is about as long as the car, and it is not attached
 * to anything -- it floats off the front right wheel, held over on its side
 * so the slide is horizontal and the shot goes out across the bonnet. That
 * is the whole character of the machine: no arms, no turret, just an
 * absurd pistol keeping station beside a race car.
 *
 * Firing kicks it. The recovery counts down from the shot, so the kick is
 * strongest on the tick it goes off and has run out by the time the next
 * one is chambered -- and the same kick shoves the car itself, in the
 * simulation, so what you see is what happened.
 */
static void mecha_add_zizin_gun(tMechaQuadList *pList,
                                const tMechaPose *pCar, const tMechaMech *pMech,
                                const tMechaMechDef *pDef, int iAimYaw,
                                int iAimPitch, float fLength, uint8_t byBody,
                                uint8_t byTrim, uint8_t byGlow)
{
  float fKick = pMech->iRecovery > 0
                  ? (float)mecha_clampi(pMech->iRecovery, 0, MECHA_GUN_KICK_TICKS)
                    / (float)MECHA_GUN_KICK_TICKS
                  : 0.0f;
  int iYaw = mecha_clampi(mecha_angle_delta(pMech->iFacing, iAimYaw),
                          -MECHA_GUN_YAW_LIMIT, MECHA_GUN_YAW_LIMIT);
  tMechaPose gun;
  float fBarrel = fLength * 0.52f;
  float fThick = fLength * 0.11f;

  /*
   * Beside the front right wheel, at about the height of one, and held over
   * on its side -- that roll is the sideways part, and it is what puts the
   * grip out to the left instead of underneath and lays the slide flat so
   * the shot goes out across the bonnet rather than over the roof.
   *
   * Firing throws it up and back. The recovery counts down from the shot,
   * so the kick is hardest on the tick it goes off and has run out by the
   * time the next round is chambered, and the same kick shoves the car in
   * the simulation: what you see is what happened.
   */
  mecha_pose_child(&gun, pCar,
                   pDef->fRadius * (1.45f + 0.12f * fKick),
                   pDef->fHeight * (0.42f + 0.42f * fKick),
                   pDef->fRadius * (1.25f - 0.80f * fKick),
                   iYaw - MECHA_GUN_ACROSS,
                   -iAimPitch + (int)((float)MECHA_GUN_KICK_PITCH * fKick),
                   MECHA_GUN_ROLL);

  /* Slide and barrel, out along the line of fire. */
  mecha_add_box(pList, &gun, 0.0f, 0.0f, fBarrel * 0.5f,
                fThick * 0.85f, fThick, fBarrel * 0.5f, byBody, byTrim, 0);
  /* The muzzle, which is the only part of it anybody looks at. */
  mecha_add_box(pList, &gun, 0.0f, 0.0f, fBarrel,
                fThick * 0.5f, fThick * 0.5f, fThick * 0.35f,
                byGlow, byGlow, MECHA_QUAD_GLOW);
  /*
   * Frame under the slide, and the grip hanging off the back of it at the
   * angle a pistol grip sits at. The frame deliberately does not start
   * where the slide does: two boxes that share a back face share a plane,
   * and two coplanar quads that overlap have no answer to which is in
   * front.
   */
  mecha_add_box(pList, &gun, 0.0f, -fThick * 1.3f, fBarrel * 0.36f,
                fThick * 0.7f, fThick * 0.32f, fBarrel * 0.30f,
                byTrim, byTrim, 0);
  {
    tMechaPose grip;

    mecha_pose_child(&grip, &gun, 0.0f, -fThick * 0.8f, -fThick * 0.2f,
                     0, MECHA_GUN_GRIP_RAKE, 0);
    mecha_add_box(pList, &grip, 0.0f, -fLength * 0.17f, 0.0f,
                  fThick * 0.62f, fLength * 0.17f, fThick * 0.9f,
                  byTrim, byTrim, 0);
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_mesh_car(tMechaQuadList *pList, const tMechaWorld *pWorld,
                           int iMechIdx)
{
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  const tMechaMechDef *pDef = mecha_def_get((int)pMech->byDefIdx);
  tMechaPose pose;
  float fPlanLength;
  float fPlanHeight;
  float fScale;
  int iAimYaw;
  int iAimPitch;

  mecha_zizin_extent(&fPlanLength, &fPlanHeight);
  if (fPlanHeight <= 0.0f)
    return;
  /* Scaled by height, because height is what the roster is measured in and
   * what says this thing is a sixth of a machine. */
  fScale = pDef->fHeight / fPlanHeight;

  {
    int iYaw = pMech->iFacing;
    int iPitch = mecha_mesh_fall_pitch(pMech);
    /* Negated for the same reason as the walkers': positive roll leans
     * left, and a car sliding right should lean right. */
    int iRoll = -(int)(pMech->fLeanRoll
                       * mecha_clampf((pMech->fVelX
                                         * mecha_cos(pMech->iFacing)
                                       - pMech->fVelZ
                                         * mecha_sin(pMech->iFacing))
                                      / (pDef->fWalkSpeed > 1.0f
                                           ? pDef->fWalkSpeed : 1.0f),
                                      -1.0f, 1.0f));

    mecha_mesh_attitude(pMech, &iYaw, &iPitch, &iRoll);
    mecha_pose_build(&pose, iYaw, iPitch, iRoll,
                     pMech->fX, pMech->fY, pMech->fZ, 1.0f);
  }

  mecha_add_zizin_body(pList, &pose, fScale, 0.0f, pDef->abyPalette[0],
                       pDef->abyPalette[1]);

  mecha_mech_aim(pWorld, iMechIdx, &iAimYaw, &iAimPitch);
  iAimPitch = mecha_clampi(iAimPitch, -MECHA_ARM_PITCH_LIMIT,
                           MECHA_ARM_PITCH_LIMIT);
  mecha_add_zizin_gun(pList, &pose, pMech, pDef, iAimYaw, iAimPitch,
                      fPlanLength * fScale, pDef->abyPalette[0],
                      pDef->abyPalette[1], pDef->abyPalette[3]);
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_mech(tMechaQuadList *pList, const tMechaWorld *pWorld,
                     int iMechIdx)
{
  const tMechaMech *pMech;
  const tMechaMechDef *pDef;
  tMechaPose pose;
  uint8_t byBody;
  uint8_t byTrim;
  uint8_t byJoint;
  uint8_t byGlow;
  float fHeight;
  float fRadius;
  float fSwing;
  float fLegLength;
  float fShoulder;
  float fTorso;
  float fLimb;
  float fHead;
  float fGun;
  float fVertical;
  float fLateral;
  float fAnkle;
  float fLegSpan;
  float fThighLen;
  float fShinLen;
  float fLift = 0.0f;
  int aiThigh[2];
  int aiKnee[2];
  int aiRoll[2];
  int iSide;
  int iRoll;
  bool bAirborne;
  tMechaPose torso;

  if (!pList || !pWorld || iMechIdx < 0 || iMechIdx >= MECHA_MAX_MECHS)
    return;
  pMech = &pWorld->aMechs[iMechIdx];
  if (!pMech->bActive)
    return;

  /* Invulnerability after a knockdown flickers the mech, the same way the
   * gauge tells you about boost: state the player has to read is state the
   * player can see. */
  if (pMech->iInvulnTicks > 0 && ((pWorld->iTick / 3) & 1))
    return;

  pDef = mecha_def_get((int)pMech->byDefIdx);
  if (pDef->bWheeled) {
    /* No legs to walk, no arms to aim: everything below this line is a
     * skeleton the one machine on wheels does not have. */
    mecha_mesh_car(pList, pWorld, iMechIdx);
    return;
  }
  byBody = pDef->abyPalette[0];
  byTrim = pDef->abyPalette[1];
  byJoint = pDef->abyPalette[2];
  byGlow = pDef->abyPalette[3];
  fHeight = pDef->fHeight;
  fRadius = pDef->fRadius;
  bAirborne = pMech->fY > mecha_arena_ground_height(&pWorld->arena, pMech->fX,
                                                    pMech->fZ, pMech->fY)
                          + 0.05f * MECHA_METRE;

  /*
   * Lean into the direction of travel, scaled by how much of it is
   * sideways. fLeanRoll is smoothed by the simulation so this never snaps.
   *
   * Negated, because positive roll lifts the right side and so leans the
   * machine left: without the minus this leant away from the direction of
   * travel, which is what the comment above has always said it should not
   * do. A machine boosting to its right leans right, the way anything on
   * wheels or blades does.
   */
  fLateral = (pMech->fVelX * mecha_cos(pMech->iFacing)
              - pMech->fVelZ * mecha_sin(pMech->iFacing));
  if (pDef->fDashSpeed > 1.0f)
    fLateral /= pDef->fDashSpeed;
  iRoll = -(int)(pMech->fLeanRoll * mecha_clampf(fLateral, -1.0f, 1.0f));

  /* Zero means one, so a machine that never declares a build still gets the
   * proportions the mesh was originally written around. */
  fShoulder = pDef->fBuildShoulder > 0.0f ? pDef->fBuildShoulder : 1.0f;
  fTorso    = pDef->fBuildTorso    > 0.0f ? pDef->fBuildTorso    : 1.0f;
  fLimb     = pDef->fBuildLimb     > 0.0f ? pDef->fBuildLimb     : 1.0f;
  fHead     = pDef->fBuildHead     > 0.0f ? pDef->fBuildHead     : 1.0f;
  fGun      = pDef->fBuildGun      > 0.0f ? pDef->fBuildGun      : 1.0f;

  /* Guard used to squash the whole machine down to two thirds. It bends its
   * knees now instead, which is the same read and an honest one. */
  fVertical = 1.0f;
  fAnkle = 0.07f * fHeight;

  /* --- the walk ---------------------------------------------------------
   *
   * Both legs run the same cycle half a stride apart, backwards when the
   * machine is backing up. The angles come first, then the body is dropped
   * onto whichever foot reaches lowest, so a crouch sinks and a stride bobs
   * without either being animated as such.
   */
  {
    float fPhase = pMech->fStepPhase - (float)(int)pMech->fStepPhase;
    bool bDash = pMech->byMove == MECHA_MOVE_DASH;
    int iGait;

    if (pMech->byMove == MECHA_MOVE_GUARD)
      iGait = MECHA_GAIT_GUARD;
    else if (bDash && bAirborne)
      iGait = MECHA_GAIT_AIRDASH;
    else if (bDash)
      iGait = MECHA_GAIT_SKATE;
    else if (bAirborne)
      iGait = MECHA_GAIT_AIR;
    else if (pMech->byMove == MECHA_MOVE_WALK)
      iGait = MECHA_GAIT_WALK;
    else
      iGait = MECHA_GAIT_STANCE;

    if (pMech->bLegsBackward)
      fPhase = 1.0f - fPhase;
    /* The glide is timed rather than paced by the ground it covers; see
     * MECHA_SKATE_TICKS. */
    if (iGait == MECHA_GAIT_SKATE)
      fPhase = (float)(pWorld->iTick % MECHA_SKATE_TICKS)
               / (float)MECHA_SKATE_TICKS;
    fLegSpan = 0.47f * fHeight - fAnkle;
    fThighLen = 0.52f * fLegSpan;
    fShinLen = fLegSpan - fThighLen;

    mecha_leg_angles(iGait, fPhase, 0, pWorld->iTick, pMech->fCombat,
                     &aiThigh[0], &aiKnee[0], &aiRoll[0]);
    mecha_leg_angles(iGait, fPhase + 0.5f, 1, pWorld->iTick, pMech->fCombat,
                     &aiThigh[1], &aiKnee[1], &aiRoll[1]);
    if (!bAirborne) {
      float afReach[2];

      for (iSide = 0; iSide < 2; iSide++)
        afReach[iSide] = mecha_leg_reach(aiThigh[iSide], aiKnee[iSide],
                                         fThighLen, fShinLen);

      if (mecha_gait_plants(iGait)) {
        /*
         * Both feet down. The floor is as far as the shorter leg can reach
         * once its own hip roll is counted, and the other leg makes up the
         * difference by rolling further out -- which is not a fudge, it is
         * how the pose works: a skater at full stretch has its pushing leg
         * out to the side precisely because it is straight, and a machine
         * standing with its feet apart has its hips open for the same
         * reason. Take the difference out of the knees instead and the
         * stance has no width to it.
         */
        float fFloor = afReach[0] * mecha_cos(aiRoll[0]);
        float fOther = afReach[1] * mecha_cos(aiRoll[1]);

        if (fOther < fFloor)
          fFloor = fOther;
        for (iSide = 0; iSide < 2; iSide++) {
          float fWant = afReach[iSide] > 0.0f ? fFloor / afReach[iSide]
                                              : 1.0f;

          aiRoll[iSide] = (int)(acosf(mecha_clampf(fWant, -1.0f, 1.0f))
                                * (float)MECHA_ANGLE_FULL / 6.28318531f);
        }
        fLift = fFloor - fLegSpan;
      } else {
        fLift = (afReach[0] > afReach[1] ? afReach[0] : afReach[1])
                - fLegSpan;
      }
    }
  }

  /*
   * The root pose is the legs, and the legs are not the machine: they point
   * along the line of travel while the shoulders hold the aim. Only a fall
   * pitches the whole thing over -- a dash lean belongs to the torso, which
   * is why the pitch is split in two.
   */
  {
    /* The whole machine, body and legs together: the tilt that answers the
     * stick belongs to the machine, not to its torso, which is the
     * difference between leaning and merely turning at the waist. */
    int iPoseYaw = pMech->iLegYaw;
    int iPosePitch = mecha_mesh_fall_pitch(pMech);
    int iPoseRoll = iRoll;

    mecha_mesh_attitude(pMech, &iPoseYaw, &iPosePitch, &iPoseRoll);
    mecha_pose_build(&pose, iPoseYaw, iPosePitch, iPoseRoll,
                     pMech->fX, pMech->fY + fLift, pMech->fZ, fVertical);
  }

  /* --- legs -------------------------------------------------------------- */
  for (iSide = 0; iSide < 2; iSide++) {
    float fSide = iSide == 0 ? -1.0f : 1.0f;
    tMechaPose hip;
    tMechaPose thigh;
    tMechaPose shin;
    tMechaPose foot;

    /*
     * Rolled out at the hip, so a stance has width and a glide has an edge
     * to push off. Positive roll walks the limb towards +x, so the side it
     * is on decides the sign.
     *
     * It is a frame of its own rather than a roll on the thigh, and that is
     * not tidiness: rolled first and swung afterwards, the whole leg tips
     * outwards as one and its foot lands exactly cos(roll) of the way down,
     * which is what lets the planting solve above pick a roll and be right.
     * Roll the thigh itself and the swing happens in the unrolled plane, the
     * two rotations no longer commute, and the feet miss the floor.
     */
    mecha_pose_child(&hip, &pose, fSide * 0.42f * fRadius * fLimb,
                     0.47f * fHeight, 0.0f, 0, 0,
                     (int)(fSide * (float)aiRoll[iSide]));
    mecha_pose_child(&thigh, &hip, 0.0f, 0.0f, 0.0f, 0, -aiThigh[iSide], 0);
    mecha_add_box(pList, &thigh, 0.0f, -0.5f * fThighLen, 0.0f,
                  0.23f * fRadius * fLimb, 0.5f * fThighLen,
                  0.24f * fRadius * fLimb, byBody, byBody, 0);
    /*
     * The knee itself, so the joint reads as a joint from any angle rather
     * than as two boxes that happen to meet. It stands proud of both the
     * thigh above it and the shin below, which is how the reference art
     * draws a knee anyway and is also the only thing keeping its faces out
     * of their planes: a joint the same width as the limb it sits on has
     * coplanar sides with it the moment the joint angle passes through
     * straight, and there is no depth buffer here to sort that out.
     */
    mecha_add_box(pList, &thigh, 0.0f, -fThighLen, 0.0f,
                  0.26f * fRadius * fLimb, 0.05f * fHeight,
                  0.27f * fRadius * fLimb, byJoint, byJoint, 0);

    /*
     * The knee bends backwards, the way a person's does: a positive pose
     * pitch swings a limb aft, so the shin takes the knee angle unnegated
     * while the thigh takes its swing negated. Both the same sign and the
     * machine becomes a bird, which is a fine thing for a mech to be but not
     * what this roster is.
     */
    mecha_pose_child(&shin, &thigh, 0.0f, -fThighLen, 0.0f, 0,
                     aiKnee[iSide], 0);
    mecha_add_box(pList, &shin, 0.0f, -0.5f * fShinLen, 0.0f,
                  0.19f * fRadius * fLimb, 0.5f * fShinLen,
                  0.20f * fRadius * fLimb, byBody, byBody, 0);

    /*
     * The foot stays flat to the floor whatever the leg above it is doing,
     * which is the whole reason it gets a joint of its own -- and that now
     * means flat both ways. The three pitches up the chain cancel to
     * nothing by construction, so what is left of the hip above the ankle
     * is the roll alone, and giving the ankle the same roll back undoes it
     * exactly. Without it a splayed leg lands on the outer edge of its foot
     * and drives the inner corner through the floor.
     */
    mecha_pose_child(&foot, &shin, 0.0f, -fShinLen, 0.0f, 0,
                     aiThigh[iSide] - aiKnee[iSide],
                     -(int)(fSide * (float)aiRoll[iSide]));
    mecha_add_box(pList, &foot, 0.0f, -0.5f * fAnkle, 0.10f * fRadius,
                  0.27f * fRadius * fLimb, 0.5f * fAnkle,
                  0.42f * fRadius * fLimb, byTrim, byTrim, 0);
  }

  /* --- torso ------------------------------------------------------------
   *
   * Turned off the legs by whatever the heading differs from the stance,
   * and carrying the lean, so a machine strafing across your guns is walking
   * sideways with its shoulders still square to you.
   */
  mecha_pose_child(&torso, &pose, 0.0f, 0.47f * fHeight, 0.0f,
                   mecha_angle_delta(pMech->iLegYaw, pMech->iFacing),
                   mecha_mesh_lean_pitch(pMech), 0);

  /* Hips, chest, chest plate. */
  mecha_add_box(pList, &torso, 0.0f, 0.0f, 0.0f,
                0.62f * fRadius * fTorso, 0.07f * fHeight,
                0.42f * fRadius * fTorso, byJoint, byJoint, 0);
  mecha_add_box(pList, &torso, 0.0f, 0.17f * fHeight, 0.02f * fRadius,
                0.72f * fRadius * fTorso, 0.13f * fHeight,
                0.50f * fRadius * fTorso, byBody, byTrim, 0);
  mecha_add_box(pList, &torso, 0.0f, 0.19f * fHeight,
                0.52f * fRadius * fTorso,
                0.50f * fRadius * fTorso, 0.09f * fHeight, 0.06f * fRadius,
                byTrim, byTrim, 0);

  /* Thruster pack. */
  mecha_add_box(pList, &torso, 0.0f, 0.19f * fHeight,
                -0.56f * fRadius * fTorso,
                0.50f * fRadius * fTorso, 0.11f * fHeight, 0.16f * fRadius,
                byJoint, byJoint, 0);

  /* --- arms -------------------------------------------------------------
   *
   * Each arm is a shoulder, an elbow and the gun the forearm carries, and
   * the whole chain is aimed: the shoulder turns and elevates onto the line
   * the weapons are pointing down, which under a held lock is the line to
   * the target. Firing kicks the arm that fired.
   */
  {
    int iAimYaw;
    int iAimPitch;
    int iArmYaw;
    float fReady = mecha_clampf(pMech->fCombat, 0.0f, 1.0f);
    float fUpper = 0.20f * fHeight;
    float fFore = 0.17f * fHeight;

    mecha_mech_aim(pWorld, iMechIdx, &iAimYaw, &iAimPitch);
    iArmYaw = mecha_clampi(mecha_angle_delta(pMech->iFacing, iAimYaw),
                           -MECHA_ARM_YAW_LIMIT, MECHA_ARM_YAW_LIMIT);
    iAimPitch = mecha_clampi(iAimPitch, -MECHA_ARM_PITCH_LIMIT,
                             MECHA_ARM_PITCH_LIMIT);

    for (iSide = 0; iSide < 2; iSide++) {
      float fSide = iSide == 0 ? -1.0f : 1.0f;
      int iSlot = iSide == 0 ? MECHA_SLOT_LEFT : MECHA_SLOT_RIGHT;
      int iKick = 0;
      tMechaPose shoulder;
      tMechaPose upper;
      tMechaPose fore;

      if (pMech->iRecovery > 0 && pMech->iLastFiredSlot == iSlot)
        iKick = MECHA_ARM_RECOIL
                * mecha_clampi(pMech->iRecovery, 0, 6) / 6;

      /* Shoulder pauldron, on the torso rather than on the arm: it is armour
       * bolted to the machine, not something the elbow swings. */
      mecha_add_box(pList, &torso, fSide * 1.00f * fRadius * fShoulder,
                    0.29f * fHeight, 0.0f,
                    0.30f * fRadius * fShoulder, 0.09f * fHeight * fShoulder,
                    0.36f * fRadius * fShoulder, byTrim, byTrim, 0);

      /*
       * And whether it is holding them up at all. A machine with nothing
       * locked and nothing in flight lets the whole chain unfold: the
       * shoulder stops tracking, the elbow gives up its right angle, and
       * the guns end up pointed at the floor. It is the only way to tell at
       * a glance which of two machines across the arena is about to shoot
       * you, and it costs nothing to read.
       */
      mecha_pose_child(&shoulder, &torso,
                       fSide * 0.98f * fRadius * fShoulder, 0.27f * fHeight,
                       0.0f, (int)((float)iArmYaw * fReady),
                       (int)((float)(-iAimPitch + iKick) * fReady), 0);
      mecha_pose_child(&upper, &shoulder, 0.0f, 0.0f, 0.0f, 0,
                       (int)(-(float)MECHA_ARM_DROOP * fReady), 0);
      mecha_add_box(pList, &upper, 0.0f, -0.5f * fUpper, 0.0f,
                    0.16f * fRadius * fLimb, 0.5f * fUpper,
                    0.16f * fRadius * fLimb, byBody, byBody, 0);
      /* Proud of both the upper arm and the forearm, for the reason the
       * knee is. */
      mecha_add_box(pList, &upper, 0.0f, -fUpper, 0.0f,
                    0.19f * fRadius * fLimb, 0.04f * fHeight,
                    0.20f * fRadius * fLimb, byJoint, byJoint, 0);

      /* The elbow makes up the rest of the right angle, so the forearm and
       * the gun on the end of it come out level along the line of aim --
       * and gives all but a bend of it back when the arm comes down. */
      mecha_pose_child(&fore, &upper, 0.0f, -fUpper, 0.0f, 0,
                       (int)(-(float)(MECHA_ANGLE_QUARTER - MECHA_ARM_DROOP)
                                 * fReady
                             - (float)MECHA_ARM_REST_ELBOW
                                   * (1.0f - fReady)), 0);
      mecha_add_box(pList, &fore, 0.0f, -0.5f * fFore, 0.0f,
                    0.14f * fRadius * fLimb, 0.5f * fFore,
                    0.14f * fRadius * fLimb, byTrim, byTrim, 0);
      mecha_add_box(pList, &fore, 0.0f,
                    -fFore - 0.13f * fHeight * fGun, 0.0f,
                    0.22f * fRadius * fGun, 0.13f * fHeight * fGun,
                    0.26f * fRadius * fGun, byBody, byBody, 0);
    }

    /* --- head ---------------------------------------------------------
     *
     * Looks at whoever is being tracked, within the limits of a neck, and
     * independently of both the legs it stands on and the shoulders it sits
     * between: the machine watches you even while it walks somewhere else.
     */
    {
      tMechaPose head;
      int iHeadYaw = mecha_clampi(iArmYaw, -MECHA_HEAD_YAW_LIMIT,
                                  MECHA_HEAD_YAW_LIMIT);
      /* The head goes on watching after the guns have come down -- it is the
       * arms that say whether the machine means it, not the eyes. */
      int iHeadPitch = mecha_clampi(iAimPitch, -MECHA_HEAD_PITCH_LIMIT,
                                    MECHA_HEAD_PITCH_LIMIT);

      mecha_pose_child(&head, &torso, 0.0f, 0.37f * fHeight, 0.0f,
                       iHeadYaw, -iHeadPitch, 0);
      mecha_add_box(pList, &head, 0.0f, 0.02f * fHeight, 0.05f * fRadius,
                    0.26f * fRadius * fHead, 0.05f * fHeight * fHead,
                    0.26f * fRadius * fHead, byTrim, byTrim, 0);
      mecha_add_box(pList, &head, 0.0f, 0.03f * fHeight,
                    0.30f * fRadius * fHead,
                    0.20f * fRadius * fHead, 0.02f * fHeight, 0.03f * fRadius,
                    byGlow, byGlow, MECHA_QUAD_GLOW);
    }
  }

  /* Thruster plume, whenever the mech is actually spending gauge. */
  if (pMech->byMove == MECHA_MOVE_DASH
      || (pMech->byMove == MECHA_MOVE_JUMP && pMech->fVelY > 0.0f)) {
    float fFlare = (0.35f + 0.12f * mecha_sin(pWorld->iTick * 2100))
                   * fRadius;

    mecha_add_box(pList, &torso, 0.0f, 0.15f * fHeight,
                  -0.74f * fRadius - fFlare,
                  0.30f * fRadius, 0.09f * fHeight, fFlare,
                  byGlow, byGlow, MECHA_QUAD_GLOW);
  }
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_shadows(tMechaQuadList *pList, const tMechaWorld *pWorld)
{
  int i;

  if (!pList || !pWorld)
    return;

  for (i = 0; i < MECHA_MAX_MECHS; i++) {
    const tMechaMech *pMech = &pWorld->aMechs[i];
    const tMechaMechDef *pDef;
    float fGround;
    float fSize;

    if (!mecha_mech_alive(pMech))
      continue;
    pDef = mecha_def_get((int)pMech->byDefIdx);

    /* The shadow sits on whatever the mech is standing over, so a mech on
     * top of a box casts onto the box rather than onto the floor below it. */
    fGround = mecha_arena_ground_height(&pWorld->arena, pMech->fX, pMech->fZ,
                                        pMech->fY);
    /* Shrinking with altitude is the only cue the player gets for how high
     * an airborne mech actually is. */
    fSize = pDef->fRadius
            * mecha_clampf(1.3f - (pMech->fY - fGround)
                                  / (28.0f * MECHA_METRE), 0.45f, 1.3f);
    /*
     * Staggered by index. Two shadows at exactly the same height overlap in
     * one plane, and which of them wins where they cross is then decided by
     * float noise -- a millimetre apiece costs nothing and settles it.
     */
    mecha_add_floor_quad(pList, pMech->fX - fSize, pMech->fZ - fSize,
                         pMech->fX + fSize, pMech->fZ + fSize,
                         fGround + (0.04f + 0.004f * (float)i) * MECHA_METRE,
                         MECHA_SHADE_SHADOW,
                         MECHA_QUAD_TWO_SIDED | MECHA_QUAD_SHADOW);
  }
}

//-------------------------------------------------------------------------------------------------
/* Camera-facing geometry */

/* Tags the quad most recently added to the list. Every add appends exactly
 * one, so this is simply "the billboard I just made". */
/*
 * Tags every quad added since iFirst. A box is six faces, and the one
 * looking at the sky wants a roof rather than a wall -- picked off the
 * normal rather than off the order the faces happen to be built in, so it
 * stays right if that order ever changes.
 */
static void mecha_tag_box(tMechaQuadList *pList, int iFirst, int iBank,
                          int iSide, int iTop)
{
  int i;

  if (!pList || iBank == MECHA_TEX_NONE)
    return;
  for (i = iFirst; i < pList->iCount; i++) {
    pList->paQuads[i].byTexBank = (uint8_t)iBank;
    pList->paQuads[i].byTile = pList->paQuads[i].afNormal[1] > 0.5f
                                 ? (uint8_t)iTop : (uint8_t)iSide;
  }
}

static void mecha_tag_texture(tMechaQuadList *pList, int iBank, int iTile)
{
  if (pList && pList->iCount > 0) {
    pList->paQuads[pList->iCount - 1].byTexBank = (uint8_t)iBank;
    pList->paQuads[pList->iCount - 1].byTile = (uint8_t)iTile;
  }
}

/* Walks a frame range by an effect's age, clamped at both ends. */
static int mecha_sprite_frame(int iFirst, int iLast, float fAge)
{
  int iCount = iLast - iFirst + 1;
  int iStep = (int)(fAge * (float)iCount);

  if (iStep < 0)
    iStep = 0;
  if (iStep >= iCount)
    iStep = iCount - 1;
  return iFirst + iStep;
}

/*
 * The plasma frames boil on a fixed cadence rather than over a fraction of
 * a life. A shot in flight has no age that means anything to look at -- a
 * beam that lives a third of a second and a lobbed charge that arcs for two
 * should shimmer at the same rate -- so this walks the sequence by ticks and
 * wraps, where the effect sprites walk theirs once and stop.
 */
#define MECHA_PLASMA_TICKS_PER_FRAME 2

void mecha_mesh_set_sprites(bool bAvailable)
{
  s_bSprites = bAvailable;
}

void mecha_mesh_set_car_skin(bool bAvailable)
{
  s_bCarSkin = bAvailable;
}

static int mecha_plasma_frame(int iAge)
{
  int iCount = MECHA_SPRITE_PLASMA_LAST - MECHA_SPRITE_PLASMA_FIRST + 1;
  int iStep;

  if (iAge < 0)
    iAge = 0;
  iStep = (iAge / MECHA_PLASMA_TICKS_PER_FRAME) % iCount;
  return MECHA_SPRITE_PLASMA_FIRST + iStep;
}

static void mecha_add_billboard(tMechaQuadList *pList, int iCameraYaw,
                                float fX, float fY, float fZ, float fSize,
                                uint8_t byPalette)
{
  float fRightX = mecha_cos(iCameraYaw);
  float fRightZ = -mecha_sin(iCameraYaw);
  float afVert[4][3];

  afVert[0][0] = fX - fRightX * fSize;
  afVert[0][1] = fY - fSize;
  afVert[0][2] = fZ - fRightZ * fSize;
  afVert[1][0] = fX + fRightX * fSize;
  afVert[1][1] = fY - fSize;
  afVert[1][2] = fZ + fRightZ * fSize;
  afVert[2][0] = fX + fRightX * fSize;
  afVert[2][1] = fY + fSize;
  afVert[2][2] = fZ + fRightZ * fSize;
  afVert[3][0] = fX - fRightX * fSize;
  afVert[3][1] = fY + fSize;
  afVert[3][2] = fZ - fRightZ * fSize;
  mecha_quads_add(pList, afVert, byPalette,
                  MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
}

/*
 * A billboard standing on the ground rather than centred on a point: its
 * bottom edge is at fBase and it is as tall as it is wide, which is what a
 * tree is. No glow -- a self-lit tree is a lamp -- so it sorts on its own
 * middle like any other quad, which for something standing upright on the
 * floor is where it actually is.
 */
static void mecha_add_upright_billboard(tMechaQuadList *pList, int iCameraYaw,
                                        float fX, float fBase, float fZ,
                                        float fHeight, uint8_t byPalette)
{
  float fRightX = mecha_cos(iCameraYaw);
  float fRightZ = -mecha_sin(iCameraYaw);
  float fHalf = fHeight * 0.5f;
  float afVert[4][3];

  afVert[0][0] = fX - fRightX * fHalf;
  afVert[0][1] = fBase;
  afVert[0][2] = fZ - fRightZ * fHalf;
  afVert[1][0] = fX + fRightX * fHalf;
  afVert[1][1] = fBase;
  afVert[1][2] = fZ + fRightZ * fHalf;
  afVert[2][0] = fX + fRightX * fHalf;
  afVert[2][1] = fBase + fHeight;
  afVert[2][2] = fZ + fRightZ * fHalf;
  afVert[3][0] = fX - fRightX * fHalf;
  afVert[3][1] = fBase + fHeight;
  afVert[3][2] = fZ - fRightZ * fHalf;
  mecha_quads_add(pList, afVert, byPalette, MECHA_QUAD_TWO_SIDED);
}

//-------------------------------------------------------------------------------------------------

/* A streak along the segment the shot covered this tick, widened towards the
 * camera. This is what makes a fast round readable at 60 Hz instead of a dot
 * that teleports across the arena. */
static void mecha_add_tracer(tMechaQuadList *pList, int iCameraYaw,
                             float fX0, float fY0, float fZ0,
                             float fX1, float fY1, float fZ1,
                             float fWidth, uint8_t byPalette)
{
  float fForwardX = mecha_sin(iCameraYaw);
  float fForwardZ = mecha_cos(iCameraYaw);
  float fAxisX = fX1 - fX0;
  float fAxisY = fY1 - fY0;
  float fAxisZ = fZ1 - fZ0;
  float fLength = mecha_length3(fAxisX, fAxisY, fAxisZ);
  float fSideX;
  float fSideY;
  float fSideZ;
  float fSideLength;
  float afVert[4][3];

  if (fLength < fWidth) {
    mecha_add_billboard(pList, iCameraYaw, fX1, fY1, fZ1, fWidth, byPalette);
    return;
  }

  fAxisX /= fLength;
  fAxisY /= fLength;
  fAxisZ /= fLength;

  /* Perpendicular to both the flight path and the view direction, so the
   * streak keeps its width whatever angle it is seen from. */
  fSideX = fAxisY * fForwardZ - fAxisZ * 0.0f;
  fSideY = fAxisZ * fForwardX - fAxisX * fForwardZ;
  fSideZ = fAxisX * 0.0f - fAxisY * fForwardX;
  fSideLength = mecha_length3(fSideX, fSideY, fSideZ);
  if (fSideLength < 1e-4f) {
    /* Flying straight at or away from the camera. */
    fSideX = mecha_cos(iCameraYaw);
    fSideY = 0.0f;
    fSideZ = -mecha_sin(iCameraYaw);
    fSideLength = 1.0f;
  }
  fSideX = fSideX / fSideLength * fWidth;
  fSideY = fSideY / fSideLength * fWidth;
  fSideZ = fSideZ / fSideLength * fWidth;

  afVert[0][0] = fX0 - fSideX; afVert[0][1] = fY0 - fSideY; afVert[0][2] = fZ0 - fSideZ;
  afVert[1][0] = fX1 - fSideX; afVert[1][1] = fY1 - fSideY; afVert[1][2] = fZ1 - fSideZ;
  afVert[2][0] = fX1 + fSideX; afVert[2][1] = fY1 + fSideY; afVert[2][2] = fZ1 + fSideZ;
  afVert[3][0] = fX0 + fSideX; afVert[3][1] = fY0 + fSideY; afVert[3][2] = fZ0 + fSideZ;
  mecha_quads_add(pList, afVert, byPalette,
                  MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
}

//-------------------------------------------------------------------------------------------------

/*
 * The roster paints its tracers in six colours (mecha_defs.c names them
 * PAL_TRACER_*), and a bolt is drawn from whichever recoloured copy of the
 * plasma frames sits nearest to that. Anything unrecognised keeps the blue
 * the frames were drawn in.
 */
int mecha_bolt_bank(uint8_t byPalette)
{
  switch (byPalette) {
  case 171:                       /* orange */
  case 206:                       /* amber  */
  case 34:                        /* sand   */
    return MECHA_TEX_EFFECT_WARM;
  case 192:                       /* violet */
    return MECHA_TEX_EFFECT_VIOLET;
  case 255:                       /* green  */
    return MECHA_TEX_EFFECT_GREEN;
  default:
    return MECHA_TEX_EFFECT;      /* cyan and white are close enough to it */
  }
}

//-------------------------------------------------------------------------------------------------
/* Draw order */

/*
 * A quad is "broad" when it is big enough for the far end of it to be a long
 * way further off than the middle. Two and a half metres is the line: floor
 * tiles and the tops of cover are broad, the panels a mech is built from are
 * not, and it matters which side of it a quad falls on -- see below.
 */
#define MECHA_BROAD_QUAD MECHA_M(2.5f)

float mecha_quad_depth_key(const tMechaQuad *pQuad, const float afEye[3],
                           const float afForward[3])
{
  float afDepth[4];
  float fCentre = 0.0f;
  float fMin;
  float fMax;
  float fSpanX = 0.0f;
  float fSpanZ = 0.0f;
  int v;

  for (v = 0; v < 4; v++) {
    afDepth[v] = (pQuad->afVert[v][0] - afEye[0]) * afForward[0]
               + (pQuad->afVert[v][1] - afEye[1]) * afForward[1]
               + (pQuad->afVert[v][2] - afEye[2]) * afForward[2];
    fCentre += afDepth[v] * 0.25f;
  }
  fMin = afDepth[0];
  fMax = afDepth[0];
  for (v = 1; v < 4; v++) {
    if (afDepth[v] < fMin)
      fMin = afDepth[v];
    if (afDepth[v] > fMax)
      fMax = afDepth[v];
  }

  /*
   * There is no depth buffer, so a quad is either drawn before another one
   * or after it, whole. For most geometry the middle of the quad is the
   * honest answer to which, and for two things it is not.
   *
   * A shadow lying on the floor is the first. Its middle can easily be
   * further off than the middle of a floor tile it covers, and then the
   * tile is painted over the top of it and the shadow is cut in half along
   * a tile edge that moves as the camera does. Sorting the decal by its
   * nearest corner and the ground beneath it by its farthest fixes that
   * both ways round: whichever of the two is larger, the ground's far
   * corner is behind the decal's near one, so the ground always goes down
   * first.
   *
   * Broad horizontal surfaces are the second, and it is the same argument
   * seen from the other side -- a floor tile stretching away under a
   * machine standing on it has to be drawn before the machine, and its far
   * corner is what says so.
   */
  if (pQuad->byFlags & (MECHA_QUAD_SHADOW | MECHA_QUAD_DECAL))
    return fMin;

  /*
   * Self-lit geometry -- blasts, flames, tracers, a visor -- is drawn on top
   * of whatever it is going off inside. A blast centred on a machine
   * intersects it, and per-quad sorting then lets some of the machine's
   * panels paint over the fireball and not others, which as the camera
   * moves is a fireball with a hole in it that swims about. Pulling the key
   * forward by the sprite's own size, capped, sorts it as though it stood
   * clear in front of the thing it is engulfing. The cap is what stops a
   * long tracer claiming to be metres nearer than it is.
   */
  if (pQuad->byFlags & MECHA_QUAD_GLOW) {
    /*
     * Pulled forward by the sprite's own half-width, which is exactly the
     * radius of the volume it stands for: everything inside that volume is
     * then outranked and everything outside it is not, so a shoulder well
     * clear of the fireball still occludes it. The narrower of the two
     * edges is the one measured, because a tracer is a long thin quad and
     * has no business claiming to be half its length nearer than it is.
     */
    float fEdgeA = mecha_length3(pQuad->afVert[1][0] - pQuad->afVert[0][0],
                                 pQuad->afVert[1][1] - pQuad->afVert[0][1],
                                 pQuad->afVert[1][2] - pQuad->afVert[0][2]);
    float fEdgeB = mecha_length3(pQuad->afVert[2][0] - pQuad->afVert[1][0],
                                 pQuad->afVert[2][1] - pQuad->afVert[1][1],
                                 pQuad->afVert[2][2] - pQuad->afVert[1][2]);
    float fReach = 0.5f * (fEdgeA < fEdgeB ? fEdgeA : fEdgeB)
                 + MECHA_M(0.4f);

    if (fReach > MECHA_M(6.0f))
      fReach = MECHA_M(6.0f);
    return fMin - fReach;
  }

  if (pQuad->afNormal[1] > 0.9f || pQuad->afNormal[1] < -0.9f) {
    float fLowX = pQuad->afVert[0][0];
    float fHighX = fLowX;
    float fLowZ = pQuad->afVert[0][2];
    float fHighZ = fLowZ;

    for (v = 1; v < 4; v++) {
      if (pQuad->afVert[v][0] < fLowX)
        fLowX = pQuad->afVert[v][0];
      if (pQuad->afVert[v][0] > fHighX)
        fHighX = pQuad->afVert[v][0];
      if (pQuad->afVert[v][2] < fLowZ)
        fLowZ = pQuad->afVert[v][2];
      if (pQuad->afVert[v][2] > fHighZ)
        fHighZ = pQuad->afVert[v][2];
    }
    fSpanX = fHighX - fLowX;
    fSpanZ = fHighZ - fLowZ;
    if (fSpanX > MECHA_BROAD_QUAD || fSpanZ > MECHA_BROAD_QUAD)
      return fMax;
  }
  return fCentre;
}

//-------------------------------------------------------------------------------------------------
/* Clouds */

/*
 * How many, how far out, and how big. The radius is chosen against the
 * arena rather than against the sky. The floor is a couple of hundred
 * metres across, so a dome at six hundred swung by nearly twenty degrees as
 * a player crossed it, which reads as the sky sliding rather than as the
 * machine walking; at fourteen hundred it is a few degrees. Puff size
 * scales with the radius, so pushing it out costs nothing but parallax --
 * and it is still four orders of magnitude short of straining a float. The retail dome sits ten million units out,
 * which is fine for a track renderer that was written around it and not
 * fine for this one.
 */
#define MECHA_CLOUD_COUNT   30
#define MECHA_CLOUD_RADIUS  MECHA_M(1400.0f)
#define MECHA_CLOUD_FLOOR   MECHA_DEG(7)    /* nothing below this elevation */
#define MECHA_CLOUD_CEILING MECHA_DEG(52)
/* Angle units per tick. A shade under one circuit an hour: a sky that is
 * visibly moving is a sky the player is looking at instead of the fight. */
#define MECHA_CLOUD_DRIFT   12

/* A cheap integer hash, so the sky is a pure function of the cloud's index
 * and the arena it hangs over. Nothing is stored between frames and nothing
 * is drawn from the world's random stream, which would put the look of the
 * sky at the mercy of how many shots had been fired under it. */
static uint32_t mecha_cloud_hash(uint32_t uiValue)
{
  uiValue *= 2654435761u;
  uiValue ^= uiValue >> 15;
  uiValue *= 2246822519u;
  uiValue ^= uiValue >> 13;
  return uiValue;
}

/*
 * The forest.
 *
 * The trees a machine can hide behind are boxes built out of the same
 * panels the cover is, and there are a dozen of them. These are the other
 * hundred and fifty: the game's own tree sprites, stood upright and turned
 * to face the camera, with nothing behind them at all. They do not collide,
 * they do not block a shot and they are not on the ground mesh -- they are
 * there so an arena with an invisible boundary reads as a clearing in a
 * wood rather than as a field that stops.
 *
 * Placement is a hash of the tree's index and the match seed, exactly as
 * the sky is: nothing is stored between frames, and the same match always
 * grows the same forest. They are spread over the whole outer reach and
 * simply skipped where they would stand somewhere a machine can walk, so
 * the density falls off naturally at the boundary rather than stopping at a
 * line.
 */
#define MECHA_TREE_TILE_FIRST 27
#define MECHA_TREE_TILE_COUNT 3

void mecha_mesh_scenery(tMechaQuadList *pList, const tMechaArena *pArena,
                        uint32_t uiSeed, int iCameraYaw)
{
  float fGrow;
  int iEdges;
  int i;

  if (!pList || !pArena || !s_bSprites || pArena->iBillboards <= 0
      || pArena->fOuterReach <= pArena->fHalfExtent)
    return;

  fGrow = pArena->fOuterReach / pArena->fHalfExtent;
  iEdges = pArena->byShape == MECHA_ARENA_OCTAGON ? 8 : 4;

  for (i = 0; i < pArena->iBillboards; i++) {
    uint32_t uiHash = mecha_cloud_hash((uint32_t)i * 2654435761u + uiSeed);
    uint32_t uiJitter = mecha_cloud_hash(uiHash ^ 0x9E3779B9u);
    float afFrom[2];
    float afTo[2];
    float fAlong = (float)(uiHash & 1023u) / 1024.0f;
    float fOut = (float)((uiHash >> 10) & 1023u) / 1024.0f;
    float fScale;
    float fSpread;
    float fX;
    float fZ;
    float fHigh;

    /*
     * Grown outwards off the boundary itself rather than scattered over a
     * square. A square scatter puts as many trees five hundred metres away
     * as fifty, which from inside the arena is a thin haze on the horizon
     * and nothing at the edge -- and the edge is the whole point, because
     * that is where the invisible wall is and where the player needs to
     * see a wood rather than an ending. Squaring the draw crowds them in
     * against the boundary and thins them out behind.
     */
    mecha_boundary_corner(pArena, (int)((uiHash >> 20) % (uint32_t)iEdges),
                          afFrom);
    mecha_boundary_corner(pArena,
                          (int)((uiHash >> 20) % (uint32_t)iEdges) + 1, afTo);
    fScale = 1.0f + (fGrow - 1.0f) * fOut * fOut;
    fX = (afFrom[0] + (afTo[0] - afFrom[0]) * fAlong) * fScale;
    fZ = (afFrom[1] + (afTo[1] - afFrom[1]) * fAlong) * fScale;

    /* Pushed about by roughly the spacing they would otherwise sit at, so
     * they are a wood and not a set of concentric fences. */
    fSpread = pArena->fHalfExtent * 0.22f * fScale;
    fX += ((float)(uiJitter & 2047u) / 1024.0f - 1.0f) * fSpread;
    fZ += ((float)((uiJitter >> 11) & 2047u) / 1024.0f - 1.0f) * fSpread;

    /* Not where the fight is. A sprite with no collision standing in the
     * arena is a tree you walk through, which is worse than no tree. */
    if (mecha_arena_contains(pArena, fX, fZ))
      continue;

    fHigh = MECHA_M(22.0f)
            + MECHA_M(18.0f) * (float)((uiJitter >> 24) & 255u) / 255.0f;
    mecha_add_upright_billboard(pList, iCameraYaw, fX,
                                mecha_arena_terrain_height(pArena, fX, fZ),
                                fZ, fHigh, MECHA_PAL_CANOPY);
    mecha_tag_texture(pList, MECHA_TEX_STRUCT,
                      MECHA_TREE_TILE_FIRST
                        + (int)((uiHash >> 28) % MECHA_TREE_TILE_COUNT));
  }
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_clouds(tMechaQuadList *pList, const tMechaWorld *pWorld)
{
  int i;

  if (!pList || !pWorld || !s_bSprites)
    return;

  for (i = 0; i < MECHA_CLOUD_COUNT; i++) {
    /* Seeded off the match, so every arena hangs under its own sky and the
     * same match always gets the same one. */
    uint32_t uiHash = mecha_cloud_hash((uint32_t)i * 3u + pWorld->uiSeed);
    int iAzimuth;
    int iElevation;
    float fLow;
    float fSize;
    float afDir[3];
    float afRight[3];
    float afUp[3];
    float afVert[4][3];
    float fLength;
    int iCorner;

    /* Squaring a uniform draw crowds the dome down towards the horizon,
     * which is where clouds are in any sky worth looking at and also where
     * they do the most work: a band of them along the skyline is what gives
     * a flat fill a distance. */
    fLow = (float)((uiHash >> 14) & 1023u) / 1024.0f;
    iElevation = MECHA_CLOUD_FLOOR
               + (int)((float)(MECHA_CLOUD_CEILING - MECHA_CLOUD_FLOOR)
                       * fLow * fLow);
    iAzimuth = mecha_angle_wrap((int)(uiHash & (uint32_t)(MECHA_ANGLE_FULL - 1))
                                + pWorld->iTick / MECHA_CLOUD_DRIFT);

    afDir[0] = mecha_cos(iElevation) * mecha_sin(iAzimuth);
    afDir[1] = mecha_sin(iElevation);
    afDir[2] = mecha_cos(iElevation) * mecha_cos(iAzimuth);

    /* Tangent to the dome, so every puff faces its middle -- which is where
     * the camera is, near enough, and is why these are not camera-facing
     * billboards: a billboard high overhead turns edge-on to a camera
     * underneath it and the sky develops holes. */
    afRight[0] = afDir[2];
    afRight[1] = 0.0f;
    afRight[2] = -afDir[0];
    fLength = mecha_length3(afRight[0], afRight[1], afRight[2]);
    if (fLength < 1e-4f)
      continue;
    afRight[0] /= fLength;
    afRight[2] /= fLength;
    afUp[0] = afDir[1] * afRight[2] - afDir[2] * afRight[1];
    afUp[1] = afDir[2] * afRight[0] - afDir[0] * afRight[2];
    afUp[2] = afDir[0] * afRight[1] - afDir[1] * afRight[0];

    fSize = MECHA_CLOUD_RADIUS
            * (0.055f + 0.045f * (float)((uiHash >> 24) & 255u) / 255.0f);

    for (iCorner = 0; iCorner < 4; iCorner++) {
      float fU = (iCorner == 0 || iCorner == 3) ? -fSize : fSize;
      float fV = iCorner < 2 ? -fSize : fSize;
      int iAxis;

      for (iAxis = 0; iAxis < 3; iAxis++) {
        afVert[iCorner][iAxis] = afDir[iAxis] * MECHA_CLOUD_RADIUS
                               + afRight[iAxis] * fU + afUp[iAxis] * fV;
      }
    }
    mecha_quads_add(pList, afVert, MECHA_PAL_CLOUD,
                    MECHA_QUAD_TWO_SIDED | MECHA_QUAD_GLOW);
    mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                      MECHA_SPRITE_CLOUD_FIRST
                      + (int)((uiHash >> 8) % (uint32_t)(
                          MECHA_SPRITE_CLOUD_LAST
                          - MECHA_SPRITE_CLOUD_FIRST + 1)));
  }
}

//-------------------------------------------------------------------------------------------------

void mecha_mesh_projectiles(tMechaQuadList *pList, const tMechaWorld *pWorld,
                            int iCameraYaw)
{
  int i;

  if (!pList || !pWorld)
    return;

  for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
    const tMechaProjectile *pShot = &pWorld->aProjectiles[i];

    if (!pShot->bActive)
      continue;

    switch (pShot->byKind) {
    case MECHA_PROJ_MINE: {
      /* A laid mine is a real object on the floor, not a sprite: you have to
       * be able to spot one and go round it. */
      tMechaPose pose;

      mecha_pose_build(&pose, 0, 0, 0, pShot->fX, pShot->fY, pShot->fZ, 1.0f);
      mecha_add_box(pList, &pose, 0.0f, pShot->fRadius * 0.4f, 0.0f,
                    pShot->fRadius, pShot->fRadius * 0.4f, pShot->fRadius,
                    pShot->byPalette,
                    pShot->iArmTicks > 0 ? pShot->byPalette
                                         : MECHA_PAL_TRACER_CORE,
                    pShot->iArmTicks > 0 ? 0 : MECHA_QUAD_GLOW);
      break;
    }

    case MECHA_PROJ_SHELL: {
      /*
       * The standing fireball. Drawn as a shell of puffs on its surface
       * rather than one billboard, because what has to read is where the
       * edge of it is: everything inside is being burned and everything
       * shot into it is being eaten, and a flat disc says nothing about
       * which side of that line a machine is on.
       */
      int iPuff;

      if (!s_bSprites) {
        mecha_add_billboard(pList, iCameraYaw, pShot->fX, pShot->fY,
                            pShot->fZ, pShot->fRadius, pShot->byPalette);
        break;
      }
      for (iPuff = 0; iPuff < MECHA_SHELL_PUFFS; iPuff++) {
        uint32_t uiHash = mecha_cloud_hash((uint32_t)(i * 131 + iPuff));
        int iAzimuth = (int)(uiHash & (uint32_t)(MECHA_ANGLE_FULL - 1));
        /* Spread over the sphere rather than round its waist, so it is a
         * ball from any angle. */
        int iElevation = (int)((uiHash >> 14) % (uint32_t)MECHA_ANGLE_HALF)
                         - MECHA_ANGLE_QUARTER;
        float afDir[3];
        float fSize = pShot->fRadius * 0.42f;

        afDir[0] = mecha_cos(iElevation) * mecha_sin(iAzimuth);
        afDir[1] = mecha_sin(iElevation);
        afDir[2] = mecha_cos(iElevation) * mecha_cos(iAzimuth);
        mecha_add_billboard(pList, iCameraYaw,
                            pShot->fX + afDir[0] * pShot->fRadius,
                            pShot->fY + afDir[1] * pShot->fRadius,
                            pShot->fZ + afDir[2] * pShot->fRadius,
                            fSize, pShot->byPalette);
        mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                          MECHA_SPRITE_BLAST_FIRST
                          + (int)((uiHash >> 24)
                                  % (uint32_t)(MECHA_SPRITE_BLAST_LAST
                                               - MECHA_SPRITE_BLAST_FIRST
                                               + 1)));
      }
      break;
    }

    case MECHA_PROJ_MELEE:
      /* The swing itself -- a broad bright arc rather than a projectile. */
      mecha_add_billboard(pList, iCameraYaw, pShot->fX, pShot->fY, pShot->fZ,
                          pShot->fRadius, pShot->byPalette);
      break;

    case MECHA_PROJ_BEAM:
      /*
       * Streak plus head. The streak stays flat and keeps the weapon's own
       * colour, because that colour is how a player tells whose fire is
       * crossing the arena -- a textured quad draws the frame's colours and
       * nothing else, so a plasma-skinned streak would make every machine's
       * beams the same blue. The head is small enough to read as the glow
       * at the front of the bolt rather than as the bolt itself.
       */
      mecha_add_tracer(pList, iCameraYaw, pShot->fPrevX, pShot->fPrevY,
                       pShot->fPrevZ, pShot->fX, pShot->fY, pShot->fZ,
                       pShot->fRadius, pShot->byPalette);
      if (s_bSprites) {
        mecha_add_billboard(pList, iCameraYaw, pShot->fX, pShot->fY,
                            pShot->fZ, pShot->fRadius * 1.8f,
                            pShot->byPalette);
        mecha_tag_texture(pList, mecha_bolt_bank(pShot->byPalette),
                          mecha_plasma_frame(pShot->iAge));
      }
      break;

    case MECHA_PROJ_BULLET:
      /* Solid rounds stay solid: a slug is not made of light. */
      mecha_add_tracer(pList, iCameraYaw, pShot->fPrevX, pShot->fPrevY,
                       pShot->fPrevZ, pShot->fX, pShot->fY, pShot->fZ,
                       pShot->fRadius, pShot->byPalette);
      break;

    default:
      /*
       * Homing pods and lobbed charges travel slowly enough to be looked
       * at, so they are the sprite rather than carrying one. Wider than the
       * flat square they replace: most of a keyed frame is background, so
       * the same quad reads smaller once it is textured.
       */
      mecha_add_billboard(pList, iCameraYaw, pShot->fX, pShot->fY, pShot->fZ,
                          pShot->fRadius * (s_bSprites ? 2.0f : 1.4f),
                          pShot->byPalette);
      mecha_tag_texture(pList, mecha_bolt_bank(pShot->byPalette),
                        mecha_plasma_frame(pShot->iAge));
      break;
    }
  }
}

//-------------------------------------------------------------------------------------------------

/* Where in an explosion's life it is at its widest, as a fraction. */
#define MECHA_FX_BURST_PEAK 0.33f

void mecha_mesh_effects(tMechaQuadList *pList, const tMechaWorld *pWorld,
                        int iCameraYaw)
{
  int i;

  if (!pList || !pWorld)
    return;

  for (i = 0; i < MECHA_MAX_EFFECTS; i++) {
    const tMechaEffect *pFx = &pWorld->aEffects[i];
    float fAge;
    float fSize;

    if (!pFx->bActive || pFx->iLife <= 0)
      continue;
    fAge = (float)pFx->iAge / (float)pFx->iLife;

    switch (pFx->byKind) {
    case MECHA_FX_EXPLOSION:
      /*
       * Opens fast, then collapses. There is no alpha in an indexed frame
       * buffer, so size is the only thing carrying the shape of the blast --
       * and the earlier curve grew all the way to fScale at the end of its
       * life, which meant a blast covered the most screen on the last frame
       * before it vanished. That reads as the arena being blanked and then
       * restored rather than as something exploding. Peaking a third of the
       * way in and shrinking from there reads as a burst.
       */
      fSize = fAge < MECHA_FX_BURST_PEAK
              ? pFx->fScale * (0.35f + 0.65f * (fAge / MECHA_FX_BURST_PEAK))
              : pFx->fScale * (1.0f - 0.7f * ((fAge - MECHA_FX_BURST_PEAK)
                                              / (1.0f - MECHA_FX_BURST_PEAK)));
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                        mecha_sprite_frame(MECHA_SPRITE_BLAST_FIRST,
                                           MECHA_SPRITE_BLAST_LAST, fAge));
      break;

    case MECHA_FX_EMBER: {
      /*
       * Debris cools as it falls. The ramp runs from the pale gold at the
       * top of the sky gradient back down through orange into the deep reds
       * at its zenith -- the same indices, which is not a coincidence worth
       * fighting: they are the one contiguous warm ramp the palette has,
       * they read as heat in either palette, and a particle that walks them
       * downwards is a particle going out.
       */
      static const uint8_t abyCool[] = {
        207, 204, 171, 170, 167, 230, 227, 224, 221
      };
      const int iSteps = (int)(sizeof(abyCool) / sizeof(abyCool[0]));
      int iStep = (int)(fAge * (float)iSteps);

      if (iStep < 0)
        iStep = 0;
      if (iStep >= iSteps)
        iStep = iSteps - 1;
      /* Shrinking as well as cooling, so the last frames are embers rather
       * than full-size squares blinking out. */
      fSize = pFx->fScale * (1.0f - 0.55f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, abyCool[iStep]);
      /* Alight for the first half of its life, smoke for the rest. */
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                        fAge < 0.5f
                          ? mecha_sprite_frame(MECHA_SPRITE_FIRE_FIRST,
                                               MECHA_SPRITE_FIRE_LAST,
                                               fAge * 2.0f)
                          : mecha_sprite_frame(MECHA_SPRITE_SMOKE_FIRST,
                                               MECHA_SPRITE_SMOKE_LAST,
                                               (fAge - 0.5f) * 2.0f));
      break;
    }

    case MECHA_FX_DUST:
      /*
       * A landing throws dust outwards, not upwards: a ring of flat puffs
       * sliding away from the feet along the ground, each one a frame of
       * the smoke sequence. One expanding square was the old version of
       * this, and it read as a stain spreading rather than as anything
       * being kicked up.
       */
      if (s_bSprites) {
        int iPuff;
        /* Thins out towards the end rather than vanishing at full size. */
        float fFade = fAge < 0.6f ? 1.0f : 1.0f - (fAge - 0.6f) / 0.4f;
        float fRing = pFx->fScale * (0.20f + 1.30f * fAge);
        float fPuff = pFx->fScale * (0.34f + 0.20f * fAge) * fFade;

        for (iPuff = 0; iPuff < MECHA_DUST_PUFFS; iPuff++) {
          /* Spaced evenly and then jittered off the spokes, so a landing
           * does not read as a cog. The jitter is a hash of the effect
           * slot, so it holds still for the life of the puff. */
          uint32_t uiHash = mecha_cloud_hash((uint32_t)(i * 31 + iPuff));
          int iStep = MECHA_ANGLE_FULL / MECHA_DUST_PUFFS;
          int iAngle = mecha_angle_wrap(iPuff * iStep
                                        + (int)(uiHash % (uint32_t)iStep));
          float fPx = pFx->fX + mecha_sin(iAngle) * fRing;
          float fPz = pFx->fZ + mecha_cos(iAngle) * fRing;
          /* A millimetre apiece, so overlapping puffs are never in exactly
           * the same plane fighting over which is on top. */
          float fY = pFx->fY + (0.06f + 0.004f * (float)iPuff)
                               * MECHA_METRE;

          if (fPuff <= 0.0f)
            break;
          mecha_add_floor_quad(pList, fPx - fPuff, fPz - fPuff,
                               fPx + fPuff, fPz + fPuff, fY, MECHA_PAL_SMOKE,
                               MECHA_QUAD_TWO_SIDED | MECHA_QUAD_DECAL);
          mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                            mecha_sprite_frame(MECHA_SPRITE_SMOKE_FIRST,
                                               MECHA_SPRITE_SMOKE_LAST,
                                               fAge));
        }
        break;
      }
      /* No bank: the old stain, which at least says something happened.
       * Darkens the ground rather than painting on it, so the shade level
       * goes in the low byte -- the effect's own colour would be read as a
       * level and index far past the end of shade_palette. */
      fSize = pFx->fScale * (0.5f + fAge);
      mecha_add_floor_quad(pList, pFx->fX - fSize, pFx->fZ - fSize,
                           pFx->fX + fSize, pFx->fZ + fSize,
                           pFx->fY + 0.08f * MECHA_METRE, MECHA_SHADE_DUST,
                           MECHA_QUAD_TWO_SIDED | MECHA_QUAD_SHADOW);
      break;

    case MECHA_FX_MUZZLE:
      /* The flash at the barrel is the front of the shot, so it is drawn
       * from the same sequence the shot is -- otherwise a bolt leaves a
       * flat square behind it every time one is fired. */
      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      mecha_tag_texture(pList, mecha_bolt_bank(pFx->byPalette),
                        mecha_plasma_frame(pFx->iAge));
      break;

    case MECHA_FX_IMPACT:
      /* A hit is a small explosion and walks the blast frames like one, just
       * over a much shorter life. */
      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                        mecha_sprite_frame(MECHA_SPRITE_BLAST_FIRST,
                                           MECHA_SPRITE_BLAST_LAST, fAge));
      break;

    case MECHA_FX_THRUSTER:
      /* Burning fuel, not a bolt: the fire frames, cycling, because a boost
       * lasts longer than one pass through them. */
      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
                        mecha_sprite_frame(MECHA_SPRITE_FIRE_FIRST,
                                           MECHA_SPRITE_FIRE_LAST, fAge));
      break;

    default:
      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      break;
    }
  }
}
