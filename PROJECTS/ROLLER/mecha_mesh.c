#include "mecha_mesh.h"

#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_sim.h"

#include <math.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------
/* Palette indices used only by the geometry; see mecha_arena.c for the rest. */
#define MECHA_PAL_TRACER_CORE 143

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

void mecha_mesh_arena(tMechaQuadList *pList, const tMechaArena *pArena)
{
  float fExtent;
  float fTile;
  int iRow;
  int i;

  if (!pList || !pArena)
    return;

  fExtent = pArena->fHalfExtent;
  fTile = fExtent * 2.0f / (float)MECHA_FLOOR_TILES;

  for (iRow = 0; iRow < MECHA_FLOOR_TILES; iRow++) {
    int iCol;

    for (iCol = 0; iCol < MECHA_FLOOR_TILES; iCol++) {
      float fX0 = -fExtent + fTile * (float)iCol;
      float fZ0 = -fExtent + fTile * (float)iRow;

      bool bAlternate = ((iRow + iCol) & 1) != 0;

      mecha_add_floor_quad(pList, fX0, fZ0, fX0 + fTile, fZ0 + fTile, 0.0f,
                           bAlternate ? pArena->byFloorPalette
                                      : pArena->byGridPalette,
                           0);
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
  mecha_add_panel(pList,  fExtent, -fExtent,  fExtent,  fExtent,
                  0.0f, pArena->fWallHeight, pArena->byWallPalette, 0);
  mecha_tag_texture(pList, MECHA_TEX_WORLD, pArena->byWallTile);
  mecha_add_panel(pList, -fExtent,  fExtent, -fExtent, -fExtent,
                  0.0f, pArena->fWallHeight, pArena->byWallPalette, 0);
  mecha_tag_texture(pList, MECHA_TEX_WORLD, pArena->byWallTile);
  mecha_add_panel(pList,  fExtent,  fExtent, -fExtent,  fExtent,
                  0.0f, pArena->fWallHeight, pArena->byWallPalette, 0);
  mecha_tag_texture(pList, MECHA_TEX_WORLD, pArena->byWallTile);
  mecha_add_panel(pList, -fExtent, -fExtent,  fExtent, -fExtent,
                  0.0f, pArena->fWallHeight, pArena->byWallPalette, 0);
  mecha_tag_texture(pList, MECHA_TEX_WORLD, pArena->byWallTile);

  for (i = 0; i < pArena->iObstacleCount; i++) {
    const tMechaObstacle *pBox = &pArena->aObstacles[i];
    tMechaPose pose;

    int iFirst = pList->iCount;

    mecha_pose_build(&pose, 0, 0, 0, pBox->fX, 0.0f, pBox->fZ, 1.0f);
    mecha_add_box(pList, &pose, 0.0f, pBox->fHeight * 0.5f, 0.0f,
                  pBox->fHalfX, pBox->fHeight * 0.5f, pBox->fHalfZ,
                  pBox->byPalette, pBox->byTrimPalette, 0);
    mecha_tag_box(pList, iFirst, MECHA_TEX_STRUCT, pBox->byTile,
                  pBox->byTopTile);
  }
}

//-------------------------------------------------------------------------------------------------
/* Mechs */

/* How far the body is tipped over. Falling and getting up are both driven
 * off the simulation's own timers, so the animation can never disagree with
 * when the mech is actually helpless. */
static int mecha_mesh_body_pitch(const tMechaMech *pMech)
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
  int iRoll;
  bool bAirborne;

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
  byBody = pDef->abyPalette[0];
  byTrim = pDef->abyPalette[1];
  byJoint = pDef->abyPalette[2];
  byGlow = pDef->abyPalette[3];
  fHeight = pDef->fHeight;
  fRadius = pDef->fRadius;
  bAirborne = pMech->fY > mecha_arena_ground_height(&pWorld->arena, pMech->fX,
                                                    pMech->fZ, pMech->fY)
                          + 0.05f * MECHA_METRE;

  /* Lean into the direction of travel, scaled by how much of it is sideways.
   * fLeanRoll is smoothed by the simulation so this never snaps. */
  fLateral = (pMech->fVelX * mecha_cos(pMech->iFacing)
              - pMech->fVelZ * mecha_sin(pMech->iFacing));
  if (pDef->fDashSpeed > 1.0f)
    fLateral /= pDef->fDashSpeed;
  iRoll = (int)(pMech->fLeanRoll * mecha_clampf(fLateral, -1.0f, 1.0f));

  /* Zero means one, so a machine that never declares a build still gets the
   * proportions the mesh was originally written around. */
  fShoulder = pDef->fBuildShoulder > 0.0f ? pDef->fBuildShoulder : 1.0f;
  fTorso    = pDef->fBuildTorso    > 0.0f ? pDef->fBuildTorso    : 1.0f;
  fLimb     = pDef->fBuildLimb     > 0.0f ? pDef->fBuildLimb     : 1.0f;
  fHead     = pDef->fBuildHead     > 0.0f ? pDef->fBuildHead     : 1.0f;
  fGun      = pDef->fBuildGun      > 0.0f ? pDef->fBuildGun      : 1.0f;

  fVertical = pMech->byMove == MECHA_MOVE_GUARD ? 0.66f : 1.0f;
  mecha_pose_build(&pose, pMech->iFacing, mecha_mesh_body_pitch(pMech), iRoll,
                   pMech->fX, pMech->fY, pMech->fZ, fVertical);

  /* Legs swing on distance travelled rather than on time, so a mech that
   * stops mid-stride stops mid-stride. Airborne, they tuck instead. */
  fSwing = mecha_sin((int)(pMech->fStepPhase * (float)MECHA_ANGLE_FULL));
  fLegLength = bAirborne ? 0.34f : 0.44f;
  if (bAirborne)
    fSwing *= 0.3f;

  /* Legs and feet. */
  mecha_add_box(pList, &pose, -0.42f * fRadius * fLimb,
                fLegLength * fHeight * 0.5f, fSwing * 0.35f * fRadius,
                0.24f * fRadius * fLimb, fLegLength * fHeight * 0.5f,
                0.26f * fRadius * fLimb, byBody, byBody, 0);
  mecha_add_box(pList, &pose, 0.42f * fRadius * fLimb,
                fLegLength * fHeight * 0.5f, -fSwing * 0.35f * fRadius,
                0.24f * fRadius * fLimb, fLegLength * fHeight * 0.5f,
                0.26f * fRadius * fLimb, byBody, byBody, 0);
  mecha_add_box(pList, &pose, -0.42f * fRadius * fLimb, 0.035f * fHeight,
                fSwing * 0.35f * fRadius + 0.10f * fRadius,
                0.28f * fRadius * fLimb, 0.035f * fHeight,
                0.42f * fRadius * fLimb, byTrim, byTrim, 0);
  mecha_add_box(pList, &pose, 0.42f * fRadius * fLimb, 0.035f * fHeight,
                -fSwing * 0.35f * fRadius + 0.10f * fRadius,
                0.28f * fRadius * fLimb, 0.035f * fHeight,
                0.42f * fRadius * fLimb, byTrim, byTrim, 0);

  /* Hips, torso, chest plate. */
  mecha_add_box(pList, &pose, 0.0f, 0.47f * fHeight, 0.0f,
                0.62f * fRadius * fTorso, 0.07f * fHeight,
                0.42f * fRadius * fTorso, byJoint, byJoint, 0);
  mecha_add_box(pList, &pose, 0.0f, 0.64f * fHeight, 0.02f * fRadius,
                0.72f * fRadius * fTorso, 0.13f * fHeight,
                0.50f * fRadius * fTorso, byBody, byTrim, 0);
  mecha_add_box(pList, &pose, 0.0f, 0.66f * fHeight,
                0.52f * fRadius * fTorso,
                0.50f * fRadius * fTorso, 0.09f * fHeight, 0.06f * fRadius,
                byTrim, byTrim, 0);

  /* Thruster pack. */
  mecha_add_box(pList, &pose, 0.0f, 0.66f * fHeight,
                -0.56f * fRadius * fTorso,
                0.50f * fRadius * fTorso, 0.11f * fHeight, 0.16f * fRadius,
                byJoint, byJoint, 0);

  /* Shoulders and the weapon each arm carries. The gun rides on the
   * shoulder span, so a wide machine carries its weapons further out as
   * well as carrying bigger ones. */
  mecha_add_box(pList, &pose, -1.00f * fRadius * fShoulder, 0.76f * fHeight,
                0.0f,
                0.30f * fRadius * fShoulder, 0.09f * fHeight * fShoulder,
                0.36f * fRadius * fShoulder, byTrim, byTrim, 0);
  mecha_add_box(pList, &pose, 1.00f * fRadius * fShoulder, 0.76f * fHeight,
                0.0f,
                0.30f * fRadius * fShoulder, 0.09f * fHeight * fShoulder,
                0.36f * fRadius * fShoulder, byTrim, byTrim, 0);
  mecha_add_box(pList, &pose, -1.05f * fRadius * fShoulder, 0.58f * fHeight,
                0.10f * fRadius,
                0.22f * fRadius * fGun, 0.13f * fHeight * fGun,
                0.26f * fRadius * fGun, byBody, byBody, 0);
  mecha_add_box(pList, &pose, 1.05f * fRadius * fShoulder, 0.58f * fHeight,
                0.10f * fRadius,
                0.22f * fRadius * fGun, 0.13f * fHeight * fGun,
                0.26f * fRadius * fGun, byBody, byBody, 0);

  /* Head and visor. */
  mecha_add_box(pList, &pose, 0.0f, 0.86f * fHeight, 0.05f * fRadius,
                0.26f * fRadius * fHead, 0.05f * fHeight * fHead,
                0.26f * fRadius * fHead, byTrim, byTrim, 0);
  mecha_add_box(pList, &pose, 0.0f, 0.87f * fHeight,
                0.30f * fRadius * fHead,
                0.20f * fRadius * fHead, 0.02f * fHeight, 0.03f * fRadius,
                byGlow, byGlow, MECHA_QUAD_GLOW);

  /* Thruster plume, whenever the mech is actually spending gauge. */
  if (pMech->byMove == MECHA_MOVE_DASH
      || (pMech->byMove == MECHA_MOVE_JUMP && pMech->fVelY > 0.0f)) {
    float fFlare = (0.35f + 0.12f * mecha_sin(pWorld->iTick * 2100))
                   * fRadius;

    mecha_add_box(pList, &pose, 0.0f, 0.62f * fHeight,
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
    mecha_add_floor_quad(pList, pMech->fX - fSize, pMech->fZ - fSize,
                         pMech->fX + fSize, pMech->fZ + fSize,
                         fGround + 0.04f * MECHA_METRE, MECHA_SHADE_SHADOW,
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

    case MECHA_PROJ_MELEE:
      /* The swing itself -- a broad bright arc rather than a projectile. */
      mecha_add_billboard(pList, iCameraYaw, pShot->fX, pShot->fY, pShot->fZ,
                          pShot->fRadius, pShot->byPalette);
      break;

    case MECHA_PROJ_BEAM:
    case MECHA_PROJ_BULLET:
      mecha_add_tracer(pList, iCameraYaw, pShot->fPrevX, pShot->fPrevY,
                       pShot->fPrevZ, pShot->fX, pShot->fY, pShot->fZ,
                       pShot->fRadius, pShot->byPalette);
      break;

    default:
      mecha_add_billboard(pList, iCameraYaw, pShot->fX, pShot->fY, pShot->fZ,
                          pShot->fRadius * 1.4f, pShot->byPalette);
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
      /* Kicked-up grit lies on the ground rather than facing the camera. */
      fSize = pFx->fScale * (0.5f + fAge);
      /* Darkens the ground rather than painting on it, so the shade level
       * goes in the low byte -- the effect's own colour would be read as a
       * level and index far past the end of shade_palette. */
      mecha_add_floor_quad(pList, pFx->fX - fSize, pFx->fZ - fSize,
                           pFx->fX + fSize, pFx->fZ + fSize,
                           pFx->fY + 0.08f * MECHA_METRE, MECHA_SHADE_DUST,
                           MECHA_QUAD_TWO_SIDED | MECHA_QUAD_SHADOW);
      break;

    default:
      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      break;
    }
  }
}
