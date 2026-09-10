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
/*
 * The walk cycle.
 *
 * fStepPhase counts distance rather than time -- one cycle every two metres
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
#define MECHA_LEG_SWING   MECHA_DEG(27)
#define MECHA_LEG_KNEE    MECHA_DEG(48)
#define MECHA_LEG_TUCK    MECHA_DEG(20)
#define MECHA_LEG_AIRKNEE MECHA_DEG(58)
/* A guard is a squat, and a human squat has to be deep to lower anything:
 * the knee travels forward as far as the hip drops, so the two cosines all
 * but cancel until the angles get large. Bird-legged, half of this was
 * enough; on a knee that bends the right way it is not. */
#define MECHA_LEG_SQUAT   MECHA_DEG(45)
#define MECHA_LEG_SQKNEE  MECHA_DEG(90)

static void mecha_leg_angles(float fPhase, bool bAirborne, bool bGuard,
                             int *piThigh, int *piKnee)
{
  int iAngle = (int)(fPhase * (float)MECHA_ANGLE_FULL) & (MECHA_ANGLE_FULL - 1);
  float fCos = mecha_cos(iAngle);

  if (bAirborne) {
    /* Tucked, and still swinging a little so a jump is not a statue. */
    *piThigh = MECHA_LEG_TUCK + (int)(MECHA_DEG(7) * mecha_sin(iAngle));
    *piKnee = MECHA_LEG_AIRKNEE;
    return;
  }
  if (bGuard) {
    *piThigh = MECHA_LEG_SQUAT;
    *piKnee = MECHA_LEG_SQKNEE;
    return;
  }
  *piThigh = (int)((float)MECHA_LEG_SWING * mecha_sin(iAngle));
  *piKnee = fCos > 0.0f ? (int)((float)MECHA_LEG_KNEE * fCos) : 0;
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
    bool bGuard = pMech->byMove == MECHA_MOVE_GUARD;

    if (pMech->bLegsBackward)
      fPhase = 1.0f - fPhase;
    fLegSpan = 0.47f * fHeight - fAnkle;
    fThighLen = 0.52f * fLegSpan;
    fShinLen = fLegSpan - fThighLen;

    mecha_leg_angles(fPhase, bAirborne, bGuard, &aiThigh[0], &aiKnee[0]);
    mecha_leg_angles(fPhase + 0.5f, bAirborne, bGuard, &aiThigh[1],
                     &aiKnee[1]);
    if (!bAirborne) {
      float fLeft = mecha_leg_reach(aiThigh[0], aiKnee[0], fThighLen,
                                    fShinLen);
      float fRight = mecha_leg_reach(aiThigh[1], aiKnee[1], fThighLen,
                                     fShinLen);

      fLift = (fLeft > fRight ? fLeft : fRight) - fLegSpan;
    }
  }

  /*
   * The root pose is the legs, and the legs are not the machine: they point
   * along the line of travel while the shoulders hold the aim. Only a fall
   * pitches the whole thing over -- a dash lean belongs to the torso, which
   * is why the pitch is split in two.
   */
  mecha_pose_build(&pose, pMech->iLegYaw, mecha_mesh_fall_pitch(pMech), iRoll,
                   pMech->fX, pMech->fY + fLift, pMech->fZ, fVertical);

  /* --- legs -------------------------------------------------------------- */
  for (iSide = 0; iSide < 2; iSide++) {
    float fSide = iSide == 0 ? -1.0f : 1.0f;
    tMechaPose thigh;
    tMechaPose shin;
    tMechaPose foot;

    mecha_pose_child(&thigh, &pose, fSide * 0.42f * fRadius * fLimb,
                     0.47f * fHeight, 0.0f, 0, -aiThigh[iSide], 0);
    mecha_add_box(pList, &thigh, 0.0f, -0.5f * fThighLen, 0.0f,
                  0.23f * fRadius * fLimb, 0.5f * fThighLen,
                  0.24f * fRadius * fLimb, byBody, byBody, 0);
    /* The knee itself, so the joint reads as a joint from any angle rather
     * than as two boxes that happen to meet. */
    mecha_add_box(pList, &thigh, 0.0f, -fThighLen, 0.0f,
                  0.19f * fRadius * fLimb, 0.05f * fHeight,
                  0.19f * fRadius * fLimb, byJoint, byJoint, 0);

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

    /* The foot stays flat to the floor whatever the leg above it is doing,
     * which is the whole reason it gets a joint of its own. */
    mecha_pose_child(&foot, &shin, 0.0f, -fShinLen, 0.0f, 0,
                     aiThigh[iSide] - aiKnee[iSide], 0);
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

      mecha_pose_child(&shoulder, &torso,
                       fSide * 0.98f * fRadius * fShoulder, 0.27f * fHeight,
                       0.0f, iArmYaw, -iAimPitch + iKick, 0);
      mecha_pose_child(&upper, &shoulder, 0.0f, 0.0f, 0.0f, 0,
                       -MECHA_ARM_DROOP, 0);
      mecha_add_box(pList, &upper, 0.0f, -0.5f * fUpper, 0.0f,
                    0.16f * fRadius * fLimb, 0.5f * fUpper,
                    0.16f * fRadius * fLimb, byBody, byBody, 0);
      mecha_add_box(pList, &upper, 0.0f, -fUpper, 0.0f,
                    0.14f * fRadius * fLimb, 0.04f * fHeight,
                    0.14f * fRadius * fLimb, byJoint, byJoint, 0);

      /* The elbow makes up the rest of the right angle, so the forearm and
       * the gun on the end of it come out level along the line of aim. */
      mecha_pose_child(&fore, &upper, 0.0f, -fUpper, 0.0f, 0,
                       -(MECHA_ANGLE_QUARTER - MECHA_ARM_DROOP), 0);
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

/*
 * The plasma frames boil on a fixed cadence rather than over a fraction of
 * a life. A shot in flight has no age that means anything to look at -- a
 * beam that lives a third of a second and a lobbed charge that arcs for two
 * should shimmer at the same rate -- so this walks the sequence by ticks and
 * wraps, where the effect sprites walk theirs once and stop.
 */
#define MECHA_PLASMA_TICKS_PER_FRAME 2

static bool s_bSprites = false;

void mecha_mesh_set_sprites(bool bAvailable)
{
  s_bSprites = bAvailable;
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
        mecha_tag_texture(pList, MECHA_TEX_EFFECT,
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
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
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

    case MECHA_FX_MUZZLE:
      /* The flash at the barrel is the front of the shot, so it is drawn
       * from the same sequence the shot is -- otherwise a bolt leaves a
       * flat square behind it every time one is fired. */
      fSize = pFx->fScale * (1.0f - 0.6f * fAge);
      mecha_add_billboard(pList, iCameraYaw, pFx->fX, pFx->fY, pFx->fZ,
                          fSize, pFx->byPalette);
      mecha_tag_texture(pList, MECHA_TEX_EFFECT,
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
