#include "mecha_render.h"

#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_sim.h"

#include "3d.h"
#include "func2.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------
/*
 * ROLLER's own HUD font lives in the retail sprite blocks, which the rest of
 * this mode deliberately does without -- the mechs, the arena and the effects
 * are all generated rather than loaded. A HUD that needed game data would be
 * the one asset dependency in an otherwise self-contained mode, so the mode
 * carries its own five-by-seven face instead. Each glyph is seven rows of
 * five bits, most significant bit leftmost.
 */
#define MECHA_GLYPH_W 5
#define MECHA_GLYPH_H 7
#define MECHA_GLYPH_ADVANCE (MECHA_GLYPH_W + 1)

static const uint8 s_aabyFont[][MECHA_GLYPH_H] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* space */
  { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 },   /* ! */
  { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },   /* A */
  { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
  { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },
  { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
  { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F },
  { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
  { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
  { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },
  { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
  { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 },
  { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
  { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
  { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
  { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },
  { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
  { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },
  { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
  { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },
  { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
  { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 },
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },   /* Z */
  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },   /* 0 */
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
  { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
  { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },   /* 9 */
  { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 },   /* : */
  { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 },   /* - */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04 },   /* . */
  { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 },   /* / */
  { 0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13 },   /* % */
};

/* Indices into the table above. */
#define MECHA_GLYPH_SPACE 0
#define MECHA_GLYPH_BANG  1
#define MECHA_GLYPH_ALPHA 2
#define MECHA_GLYPH_DIGIT (MECHA_GLYPH_ALPHA + 26)
#define MECHA_GLYPH_COLON (MECHA_GLYPH_DIGIT + 10)
#define MECHA_GLYPH_DASH  (MECHA_GLYPH_COLON + 1)
#define MECHA_GLYPH_DOT   (MECHA_GLYPH_COLON + 2)
#define MECHA_GLYPH_SLASH (MECHA_GLYPH_COLON + 3)
#define MECHA_GLYPH_PCT   (MECHA_GLYPH_COLON + 4)

//-------------------------------------------------------------------------------------------------
/* HUD palette. Named here for the same reason as the arena's colours. */
#define MECHA_HUD_FRAME   16
#define MECHA_HUD_TEXT    255
#define MECHA_HUD_ARMOUR  195
#define MECHA_HUD_ARMOUR_LOW 243
#define MECHA_HUD_BOOST   183
#define MECHA_HUD_BOOST_LOCKED 243
#define MECHA_HUD_AMMO    219
#define MECHA_HUD_EMPTY   130
#define MECHA_HUD_LOCK    231
#define MECHA_HUD_ENEMY   243

/* Camera framing, in metres. */
#define MECHA_CAM_BACK_NEAR  24.0f
#define MECHA_CAM_BACK_FAR   42.0f
#define MECHA_CAM_HEIGHT     20.0f
#define MECHA_CAM_FLOOR       2.5f

/* The projection reference frame the software rasteriser works in: it
 * projects into a 320x200 space and then scales by scr_size >> 6. */
#define MECHA_PROJ_VIEWDIST 270
#define MECHA_PROJ_CENTRE_X 159
#define MECHA_PROJ_CENTRE_Y 100
#define MECHA_PROJ_NEAR     90.0f

//-------------------------------------------------------------------------------------------------

static int mecha_glyph_index(char cChar)
{
  if (cChar >= 'a' && cChar <= 'z')
    cChar = (char)(cChar - 'a' + 'A');
  if (cChar >= 'A' && cChar <= 'Z')
    return MECHA_GLYPH_ALPHA + (cChar - 'A');
  if (cChar >= '0' && cChar <= '9')
    return MECHA_GLYPH_DIGIT + (cChar - '0');
  switch (cChar) {
  case '!': return MECHA_GLYPH_BANG;
  case ':': return MECHA_GLYPH_COLON;
  case '-': return MECHA_GLYPH_DASH;
  case '.': return MECHA_GLYPH_DOT;
  case '/': return MECHA_GLYPH_SLASH;
  case '%': return MECHA_GLYPH_PCT;
  default:  return MECHA_GLYPH_SPACE;
  }
}

//-------------------------------------------------------------------------------------------------

void mecha_render_fill(uint8 *pScrBuf, int iWidth, int iHeight,
                       int iX, int iY, int iW, int iH, uint8 byColour)
{
  int iRow;

  if (!pScrBuf || iW <= 0 || iH <= 0)
    return;

  if (iX < 0) { iW += iX; iX = 0; }
  if (iY < 0) { iH += iY; iY = 0; }
  if (iX + iW > iWidth)  iW = iWidth - iX;
  if (iY + iH > iHeight) iH = iHeight - iY;
  if (iW <= 0 || iH <= 0)
    return;

  for (iRow = 0; iRow < iH; iRow++)
    memset(pScrBuf + (size_t)(iY + iRow) * (size_t)iWidth + (size_t)iX,
           byColour, (size_t)iW);
}

//-------------------------------------------------------------------------------------------------

int mecha_render_text_width(int iScale, const char *szText)
{
  if (!szText || iScale < 1)
    return 0;
  return (int)strlen(szText) * MECHA_GLYPH_ADVANCE * iScale;
}

//-------------------------------------------------------------------------------------------------

int mecha_render_text(uint8 *pScrBuf, int iWidth, int iHeight,
                      int iX, int iY, int iScale, uint8 byColour,
                      const char *szText)
{
  const char *pChar;

  if (!pScrBuf || !szText)
    return iX;
  if (iScale < 1)
    iScale = 1;

  for (pChar = szText; *pChar; pChar++) {
    const uint8 *pGlyph = s_aabyFont[mecha_glyph_index(*pChar)];
    int iRow;

    for (iRow = 0; iRow < MECHA_GLYPH_H; iRow++) {
      int iCol;

      for (iCol = 0; iCol < MECHA_GLYPH_W; iCol++) {
        if (!(pGlyph[iRow] & (1u << (MECHA_GLYPH_W - 1 - iCol))))
          continue;
        mecha_render_fill(pScrBuf, iWidth, iHeight,
                          iX + iCol * iScale, iY + iRow * iScale,
                          iScale, iScale, byColour);
      }
    }
    iX += MECHA_GLYPH_ADVANCE * iScale;
  }
  return iX;
}

//-------------------------------------------------------------------------------------------------
/* Camera */

void mecha_camera_reset(tMechaCamera *pCamera)
{
  if (!pCamera)
    return;
  memset(pCamera, 0, sizeof(*pCamera));
  pCamera->bSettled = false;
}

//-------------------------------------------------------------------------------------------------

void mecha_camera_update(tMechaCamera *pCamera, const tMechaWorld *pWorld,
                         int iViewMech)
{
  const tMechaMech *pMech;
  const tMechaMechDef *pDef;
  float fFocusX;
  float fFocusY;
  float fFocusZ;
  float fBack;
  float fWantX;
  float fWantY;
  float fWantZ;
  float fGround;
  float fFlat;
  int iWantYaw;
  int iTargetIdx;

  if (!pCamera || !pWorld || iViewMech < 0 || iViewMech >= MECHA_MAX_MECHS)
    return;
  pMech = &pWorld->aMechs[iViewMech];
  if (!pMech->bActive)
    return;
  pDef = mecha_def_get((int)pMech->byDefIdx);

  /* Look along the lock rather than along the mech's nose. They agree most
   * of the time, but during the turn onto a new target the camera leading
   * the mech is what makes the lock legible. */
  iTargetIdx = pMech->iTargetIdx;
  if (iTargetIdx >= 0 && iTargetIdx < MECHA_MAX_MECHS
      && mecha_mech_alive(&pWorld->aMechs[iTargetIdx])) {
    const tMechaMech *pTarget = &pWorld->aMechs[iTargetIdx];
    float fDx = pTarget->fX - pMech->fX;
    float fDz = pTarget->fZ - pMech->fZ;
    float fRange = mecha_length2(fDx, fDz);

    iWantYaw = mecha_atan2_angle(fDx, fDz);
    /* Pull back as the fight opens up, so both mechs stay in frame. */
    fBack = MECHA_CAM_BACK_NEAR
            + (MECHA_CAM_BACK_FAR - MECHA_CAM_BACK_NEAR)
              * mecha_clampf(fRange / (90.0f * MECHA_METRE), 0.0f, 1.0f);
    /* Centre the target, not the midpoint. The camera sits behind the player
     * and looks along the lock, so anything it centres has the player's own
     * mech in front of it; centring the enemy is what pushes your machine
     * down into the foreground instead of parking it over the thing you are
     * trying to shoot. */
    fFocusX = pTarget->fX;
    fFocusZ = pTarget->fZ;
    fFocusY = mecha_mech_centre_height(pWorld, iTargetIdx);
  } else {
    iWantYaw = pMech->iFacing;
    fBack = MECHA_CAM_BACK_NEAR;
    fFocusX = pMech->fX + mecha_sin(iWantYaw) * pDef->fHeight;
    fFocusZ = pMech->fZ + mecha_cos(iWantYaw) * pDef->fHeight;
    fFocusY = mecha_mech_centre_height(pWorld, iViewMech);
  }
  fBack *= MECHA_METRE;

  fWantX = pMech->fX - mecha_sin(iWantYaw) * fBack;
  fWantZ = pMech->fZ - mecha_cos(iWantYaw) * fBack;
  fWantY = pMech->fY + MECHA_CAM_HEIGHT * MECHA_METRE;

  if (!pCamera->bSettled) {
    pCamera->fX = fWantX;
    pCamera->fY = fWantY;
    pCamera->fZ = fWantZ;
    pCamera->iYaw = iWantYaw;
    pCamera->bSettled = true;
  } else {
    /* Position eases; heading turns at a bounded rate. A camera that snaps
     * to the lock feels like it is fighting the player. */
    pCamera->fX += (fWantX - pCamera->fX) * 0.18f;
    pCamera->fY += (fWantY - pCamera->fY) * 0.22f;
    pCamera->fZ += (fWantZ - pCamera->fZ) * 0.18f;
    pCamera->iYaw = mecha_angle_approach(pCamera->iYaw, iWantYaw,
                                         MECHA_DEG(6));
  }

  /* Never let the camera sink through the floor, or through the roof of a
   * box it has drifted over. */
  fGround = mecha_arena_ground_height(&pWorld->arena, pCamera->fX,
                                      pCamera->fZ, pCamera->fY);
  if (pCamera->fY < fGround + MECHA_CAM_FLOOR * MECHA_METRE)
    pCamera->fY = fGround + MECHA_CAM_FLOOR * MECHA_METRE;

  fFlat = mecha_length2(fFocusX - pCamera->fX, fFocusZ - pCamera->fZ);
  pCamera->iPitch = fFlat > 1.0f
                    ? mecha_atan2_angle(fFocusY - pCamera->fY, fFlat)
                    : 0;
}

//-------------------------------------------------------------------------------------------------

/* The camera's basis in world space. Column order matches what the software
 * renderer expects in GameRenderProjection::view: right, up, forward. */
static void mecha_camera_basis(const tMechaCamera *pCamera,
                               float afRight[3], float afUp[3],
                               float afForward[3])
{
  float fCosYaw = mecha_cos(pCamera->iYaw);
  float fSinYaw = mecha_sin(pCamera->iYaw);
  float fCosPitch = mecha_cos(pCamera->iPitch);
  float fSinPitch = mecha_sin(pCamera->iPitch);

  afForward[0] = fSinYaw * fCosPitch;
  afForward[1] = fSinPitch;
  afForward[2] = fCosYaw * fCosPitch;

  afRight[0] = fCosYaw;
  afRight[1] = 0.0f;
  afRight[2] = -fSinYaw;

  /* up = forward x right, which for this basis is the tilted vertical. */
  afUp[0] = -fSinPitch * fSinYaw;
  afUp[1] = fCosPitch;
  afUp[2] = -fSinPitch * fCosYaw;
}

//-------------------------------------------------------------------------------------------------

bool mecha_render_project(const tMechaCamera *pCamera, int iWidth, int iHeight,
                          float fX, float fY, float fZ,
                          int *piScreenX, int *piScreenY)
{
  float afRight[3];
  float afUp[3];
  float afForward[3];
  float fDx;
  float fDy;
  float fDz;
  float fViewX;
  float fViewY;
  float fViewZ;

  if (!pCamera)
    return false;

  mecha_camera_basis(pCamera, afRight, afUp, afForward);
  fDx = fX - pCamera->fX;
  fDy = fY - pCamera->fY;
  fDz = fZ - pCamera->fZ;

  fViewZ = fDx * afForward[0] + fDy * afForward[1] + fDz * afForward[2];
  if (fViewZ < MECHA_PROJ_NEAR)
    return false;
  fViewX = fDx * afRight[0] + fDy * afRight[1] + fDz * afRight[2];
  fViewY = fDx * afUp[0] + fDy * afUp[1] + fDz * afUp[2];

  /* Same two steps the rasteriser takes: project into the 320x200 reference
   * frame, then scale that up to the real buffer. */
  if (piScreenX)
    *piScreenX = (int)((MECHA_PROJ_VIEWDIST * fViewX / fViewZ
                        + MECHA_PROJ_CENTRE_X) * (float)iWidth / 320.0f);
  if (piScreenY)
    *piScreenY = (int)((199.0f - (MECHA_PROJ_VIEWDIST * fViewY / fViewZ
                                  + MECHA_PROJ_CENTRE_Y))
                       * (float)iHeight / 200.0f);
  return true;
}

//-------------------------------------------------------------------------------------------------
/* Frame */

typedef struct
{
  float fDepth;
  int   iIndex;
} tMechaSortKey;

/* Sized once, statically: the mode allocates nothing per frame. */
static tMechaSortKey s_aSortKeys[MECHA_QUAD_CAPACITY];

//-------------------------------------------------------------------------------------------------

static int mecha_sort_compare(const void *pLeft, const void *pRight)
{
  const tMechaSortKey *pA = (const tMechaSortKey *)pLeft;
  const tMechaSortKey *pB = (const tMechaSortKey *)pRight;

  /* Farthest first: the software rasteriser has no depth buffer, so the only
   * thing keeping the arena in the right order is the order it is handed
   * over in. */
  if (pA->fDepth > pB->fDepth)
    return -1;
  if (pA->fDepth < pB->fDepth)
    return 1;
  /* Ties broken by index so the order is stable frame to frame; otherwise
   * coplanar quads flicker against each other. */
  return pA->iIndex - pB->iIndex;
}

//-------------------------------------------------------------------------------------------------

/*
 * Pulls any vertex behind the near plane forward along an edge that crosses
 * it. This keeps the polygon a quad -- which is all game_render_quad_world
 * accepts -- and it is what stops a floor tile the camera is standing on
 * from smearing across the screen when the rasteriser clamps its z.
 *
 * Returns false when the whole quad is behind the camera.
 */
static bool mecha_clip_near(float afWorld[4][3], const float afViewZ[4],
                            float fNear)
{
  bool abFront[4];
  float afZ[4];
  int iFrontCount = 0;
  int i;

  for (i = 0; i < 4; i++) {
    afZ[i] = afViewZ[i];
    abFront[i] = afZ[i] >= fNear;
    if (abFront[i])
      iFrontCount++;
  }
  if (iFrontCount == 0)
    return false;
  if (iFrontCount == 4)
    return true;

  for (i = 0; i < 4; i++) {
    int iNeighbour = -1;
    float fSpan;
    float fFraction;
    int iStep;
    int iAxis;

    if (abFront[i])
      continue;

    /* Prefer an adjacent vertex so the edge that gets shortened is a real
     * edge of the quad rather than a diagonal. */
    for (iStep = 1; iStep <= 3 && iNeighbour < 0; iStep++) {
      int iCandidate = (i + iStep) & 3;

      if (abFront[iCandidate])
        iNeighbour = iCandidate;
      iCandidate = (i + 4 - iStep) & 3;
      if (iNeighbour < 0 && abFront[iCandidate])
        iNeighbour = iCandidate;
    }
    if (iNeighbour < 0)
      return false;

    fSpan = afZ[iNeighbour] - afZ[i];
    if (fabsf(fSpan) < 1e-4f)
      return false;
    fFraction = (fNear - afZ[i]) / fSpan;
    fFraction = mecha_clampf(fFraction, 0.0f, 1.0f);
    for (iAxis = 0; iAxis < 3; iAxis++)
      afWorld[i][iAxis] += (afWorld[iNeighbour][iAxis] - afWorld[i][iAxis])
                           * fFraction;
  }
  return true;
}

//-------------------------------------------------------------------------------------------------

static void mecha_render_scene(GameRenderer *pRenderer,
                               const tMechaWorld *pWorld,
                               const tMechaCamera *pCamera,
                               int iViewMech,
                               tMechaQuad *paScratch, int iScratchCapacity)
{
  tMechaQuadList list;
  float afRight[3];
  float afUp[3];
  float afForward[3];
  int iSortCount = 0;
  int iMech;
  int i;

  if (iScratchCapacity > MECHA_QUAD_CAPACITY)
    iScratchCapacity = MECHA_QUAD_CAPACITY;

  mecha_quads_reset(&list, paScratch, iScratchCapacity);
  mecha_mesh_arena(&list, &pWorld->arena);
  mecha_mesh_shadows(&list, pWorld);
  for (iMech = 0; iMech < MECHA_MAX_MECHS; iMech++)
    mecha_mesh_mech(&list, pWorld, iMech);
  mecha_mesh_projectiles(&list, pWorld, pCamera->iYaw);
  mecha_mesh_effects(&list, pWorld, pCamera->iYaw);
  (void)iViewMech;

  mecha_camera_basis(pCamera, afRight, afUp, afForward);

  for (i = 0; i < list.iCount; i++) {
    const tMechaQuad *pQuad = &paScratch[i];
    float fCentre[3] = { 0.0f, 0.0f, 0.0f };
    float fDepth;
    int iVert;
    int iAxis;

    for (iVert = 0; iVert < 4; iVert++) {
      for (iAxis = 0; iAxis < 3; iAxis++)
        fCentre[iAxis] += pQuad->afVert[iVert][iAxis] * 0.25f;
    }

    /* Back-face rejection. Closed hulls and the arena walls are one-sided,
     * which halves the fill and stops a box's far faces painting over its
     * near ones once the depth sort puts them in the same place. */
    if (!(pQuad->byFlags & MECHA_QUAD_TWO_SIDED)) {
      float fFacing = pQuad->afNormal[0] * (fCentre[0] - pCamera->fX)
                    + pQuad->afNormal[1] * (fCentre[1] - pCamera->fY)
                    + pQuad->afNormal[2] * (fCentre[2] - pCamera->fZ);

      if (fFacing >= 0.0f)
        continue;
    }

    fDepth = (fCentre[0] - pCamera->fX) * afForward[0]
           + (fCentre[1] - pCamera->fY) * afForward[1]
           + (fCentre[2] - pCamera->fZ) * afForward[2];
    if (fDepth <= 0.0f)
      continue;

    s_aSortKeys[iSortCount].fDepth = fDepth;
    s_aSortKeys[iSortCount].iIndex = i;
    iSortCount++;
  }

  qsort(s_aSortKeys, (size_t)iSortCount, sizeof(s_aSortKeys[0]),
        mecha_sort_compare);

  for (i = 0; i < iSortCount; i++) {
    const tMechaQuad *pQuad = &paScratch[s_aSortKeys[i].iIndex];
    float afWorld[4][3];
    float afViewZ[4];
    GameRenderVertex aVerts[4];
    int iSurfaceFlags;
    int iVert;

    memcpy(afWorld, pQuad->afVert, sizeof(afWorld));
    for (iVert = 0; iVert < 4; iVert++) {
      afViewZ[iVert] = (afWorld[iVert][0] - pCamera->fX) * afForward[0]
                     + (afWorld[iVert][1] - pCamera->fY) * afForward[1]
                     + (afWorld[iVert][2] - pCamera->fZ) * afForward[2];
    }
    if (!mecha_clip_near(afWorld, afViewZ, MECHA_PROJ_NEAR))
      continue;

    for (iVert = 0; iVert < 4; iVert++) {
      aVerts[iVert].x = afWorld[iVert][0];
      aVerts[iVert].y = afWorld[iVert][1];
      aVerts[iVert].z = afWorld[iVert][2];
      /* Flat fill; the rasteriser never reads these. */
      aVerts[iVert].u = 0.0f;
      aVerts[iVert].v = 0.0f;
    }

    /* POLYFLAT takes its colour from the low byte of the surface flags, and
     * routes anything marked transparent through shadow_poly -- which is
     * exactly the translucent pass mech shadows and ground dust want.
     *
     * For that path the low byte is a shade LEVEL, not a colour: shadow_poly
     * indexes shade_palette[256 * level], and shade_palette holds only 16
     * such blocks. The mask keeps a mislabelled quad from reading past the
     * end of it -- a bad colour is a visible bug, a bad read is not. */
    iSurfaceFlags = (int)pQuad->byPalette;
    if (pQuad->byFlags & MECHA_QUAD_SHADOW)
      iSurfaceFlags = SURFACE_FLAG_TRANSPARENT | (iSurfaceFlags & 0x0F);

    /* A positive threshold below the near plane means every quad rasterises
     * directly instead of being subdivided: subdivision exists for texture
     * perspective, and none of this geometry is textured. */
    game_render_quad_world(pRenderer, aVerts, TEXTURE_HANDLE_INVALID,
                           iSurfaceFlags, 1.0f);
  }
}

//-------------------------------------------------------------------------------------------------
/* HUD */

static void mecha_hud_bar(uint8 *pScrBuf, int iWidth, int iHeight,
                          int iX, int iY, int iW, int iH,
                          float fFraction, uint8 byFill, uint8 byEmpty)
{
  int iFilled;

  fFraction = mecha_clampf(fFraction, 0.0f, 1.0f);
  iFilled = (int)((float)iW * fFraction);

  /* One-pixel frame, so a bar at zero is still visible as an empty socket
   * rather than disappearing. */
  mecha_render_fill(pScrBuf, iWidth, iHeight, iX - 1, iY - 1, iW + 2, iH + 2,
                    MECHA_HUD_FRAME);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iX, iY, iW, iH, byEmpty);
  if (iFilled > 0)
    mecha_render_fill(pScrBuf, iWidth, iHeight, iX, iY, iFilled, iH, byFill);
}

//-------------------------------------------------------------------------------------------------

static void mecha_hud_reticle(uint8 *pScrBuf, int iWidth, int iHeight,
                              const tMechaCamera *pCamera,
                              const tMechaWorld *pWorld, int iTargetIdx,
                              int iScale)
{
  int iScreenX;
  int iScreenY;
  int iArm = 5 * iScale;
  int iGap = 9 * iScale;
  int iThick = iScale;

  if (iTargetIdx < 0 || iTargetIdx >= MECHA_MAX_MECHS)
    return;
  if (!mecha_mech_alive(&pWorld->aMechs[iTargetIdx]))
    return;
  if (!mecha_render_project(pCamera, iWidth, iHeight,
                            pWorld->aMechs[iTargetIdx].fX,
                            mecha_mech_centre_height(pWorld, iTargetIdx),
                            pWorld->aMechs[iTargetIdx].fZ,
                            &iScreenX, &iScreenY))
    return;

  /* Four corner brackets rather than a full box: it marks the target without
   * covering it. */
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX - iGap,
                    iScreenY - iGap, iArm, iThick, MECHA_HUD_LOCK);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX - iGap,
                    iScreenY - iGap, iThick, iArm, MECHA_HUD_LOCK);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX + iGap - iArm,
                    iScreenY - iGap, iArm, iThick, MECHA_HUD_LOCK);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX + iGap - iThick,
                    iScreenY - iGap, iThick, iArm, MECHA_HUD_LOCK);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX - iGap,
                    iScreenY + iGap - iThick, iArm, iThick, MECHA_HUD_LOCK);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX - iGap,
                    iScreenY + iGap - iArm, iThick, iArm, MECHA_HUD_LOCK);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX + iGap - iArm,
                    iScreenY + iGap - iThick, iArm, iThick, MECHA_HUD_LOCK);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX + iGap - iThick,
                    iScreenY + iGap - iArm, iThick, iArm, MECHA_HUD_LOCK);
}

//-------------------------------------------------------------------------------------------------

static void mecha_hud_weapons(uint8 *pScrBuf, int iWidth, int iHeight,
                              const tMechaWorld *pWorld, int iMechIdx,
                              int iX, int iY, int iScale)
{
  static const char *kaszSlot[MECHA_WEAPON_SLOTS] = { "L", "C", "R" };
  const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
  int iSlot;

  for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
    const tMechaWeaponDef *pWeapon = mecha_mech_weapon(pWorld, iMechIdx, iSlot);
    int iRowY = iY + iSlot * 9 * iScale;
    int iTextX;

    if (!pWeapon)
      continue;

    iTextX = mecha_render_text(pScrBuf, iWidth, iHeight, iX, iRowY, iScale,
                               MECHA_HUD_TEXT, kaszSlot[iSlot]);
    iTextX += 2 * iScale;

    if (pMech->aiReload[iSlot] > 0) {
      /* Reloading: show the magazine filling rather than the weapon name, so
       * the wait is legible at a glance. */
      float fProgress = 1.0f - (float)pMech->aiReload[iSlot]
                               / (float)(pWeapon->iReloadTicks > 0
                                         ? pWeapon->iReloadTicks : 1);

      mecha_hud_bar(pScrBuf, iWidth, iHeight, iTextX, iRowY + iScale,
                    40 * iScale, 5 * iScale, fProgress,
                    MECHA_HUD_AMMO, MECHA_HUD_EMPTY);
    } else {
      int iPip;

      /* One pip per round left. Capped, because a suppression magazine would
       * otherwise run off the side of the screen. */
      for (iPip = 0; iPip < pWeapon->iAmmo && iPip < 12; iPip++) {
        mecha_render_fill(pScrBuf, iWidth, iHeight,
                          iTextX + iPip * 4 * iScale, iRowY + iScale,
                          3 * iScale, 5 * iScale,
                          iPip < pMech->aiAmmo[iSlot] ? MECHA_HUD_AMMO
                                                      : MECHA_HUD_EMPTY);
      }
      mecha_render_text(pScrBuf, iWidth, iHeight,
                        iTextX + 52 * iScale, iRowY, iScale,
                        MECHA_HUD_TEXT, pWeapon->szName);
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_hud_rounds(uint8 *pScrBuf, int iWidth, int iHeight,
                             const tMechaWorld *pWorld, int iMechIdx,
                             int iX, int iY, int iScale)
{
  int iWon = pWorld->aMechs[iMechIdx].iRoundsWon;
  int i;

  for (i = 0; i < pWorld->match.iRoundsToWin; i++) {
    mecha_render_fill(pScrBuf, iWidth, iHeight, iX + i * 10 * iScale, iY,
                      7 * iScale, 7 * iScale,
                      i < iWon ? MECHA_HUD_LOCK : MECHA_HUD_EMPTY);
  }
}

//-------------------------------------------------------------------------------------------------

static const char *mecha_phase_banner(const tMechaWorld *pWorld,
                                      int iViewMech)
{
  switch (pWorld->match.byPhase) {
  case MECHA_PHASE_READY:
    return "READY";
  case MECHA_PHASE_ROUND_OVER:
    if (pWorld->match.iWinnerIdx < 0)
      return "DRAW";
    return pWorld->match.iWinnerIdx == iViewMech ? "ROUND WIN" : "ROUND LOST";
  case MECHA_PHASE_MATCH_OVER:
    if (pWorld->match.iWinnerIdx < 0)
      return "DRAW";
    return pWorld->match.iWinnerIdx == iViewMech ? "VICTORY" : "DEFEAT";
  default:
    /* "FIGHT" only for the first moments of the round, then out of the way. */
    return pWorld->match.iRoundTicks
             > pWorld->match.iRoundTimeLimit - MECHA_TICK_HZ ? "FIGHT" : NULL;
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_render_hud(uint8 *pScrBuf, int iWidth, int iHeight,
                             const tMechaWorld *pWorld,
                             const tMechaCamera *pCamera, int iViewMech)
{
  const tMechaMech *pMech;
  const tMechaMechDef *pDef;
  const char *szBanner;
  char szBuffer[32];
  int iScale = iWidth / 320;
  int iBarW;
  int iTargetIdx;
  int iSeconds;

  if (iScale < 1)
    iScale = 1;
  if (iViewMech < 0 || iViewMech >= MECHA_MAX_MECHS)
    return;
  pMech = &pWorld->aMechs[iViewMech];
  if (!pMech->bActive)
    return;
  pDef = mecha_def_get((int)pMech->byDefIdx);
  iBarW = 120 * iScale;

  /* --- the player, bottom left ----------------------------------------- */

  mecha_render_text(pScrBuf, iWidth, iHeight, 10 * iScale,
                    iHeight - 60 * iScale, iScale, MECHA_HUD_TEXT,
                    pDef->szName);
  mecha_hud_bar(pScrBuf, iWidth, iHeight, 10 * iScale,
                iHeight - 50 * iScale, iBarW, 8 * iScale,
                mecha_mech_armour_fraction(pWorld, iViewMech),
                mecha_mech_armour_fraction(pWorld, iViewMech) < 0.3f
                  ? MECHA_HUD_ARMOUR_LOW : MECHA_HUD_ARMOUR,
                MECHA_HUD_EMPTY);
  /* The boost gauge turns red while it is locked out, which is the one piece
   * of state a player has to be able to read instantly. */
  mecha_hud_bar(pScrBuf, iWidth, iHeight, 10 * iScale,
                iHeight - 38 * iScale, iBarW, 5 * iScale,
                mecha_mech_boost_fraction(pWorld, iViewMech),
                pMech->bBoostLocked ? MECHA_HUD_BOOST_LOCKED : MECHA_HUD_BOOST,
                MECHA_HUD_EMPTY);
  mecha_hud_weapons(pScrBuf, iWidth, iHeight, pWorld, iViewMech,
                    10 * iScale, iHeight - 28 * iScale, iScale);
  mecha_hud_rounds(pScrBuf, iWidth, iHeight, pWorld, iViewMech,
                   10 * iScale, iHeight - 70 * iScale, iScale);

  /* --- the target, top right -------------------------------------------- */

  iTargetIdx = pMech->iTargetIdx;
  if (iTargetIdx >= 0 && iTargetIdx < MECHA_MAX_MECHS
      && pWorld->aMechs[iTargetIdx].bActive) {
    const tMechaMechDef *pTargetDef =
        mecha_def_get((int)pWorld->aMechs[iTargetIdx].byDefIdx);
    int iRightX = iWidth - iBarW - 10 * iScale;

    mecha_render_text(pScrBuf, iWidth, iHeight, iRightX, 10 * iScale,
                      iScale, MECHA_HUD_ENEMY, pTargetDef->szName);
    mecha_hud_bar(pScrBuf, iWidth, iHeight, iRightX, 20 * iScale,
                  iBarW, 8 * iScale,
                  mecha_mech_armour_fraction(pWorld, iTargetIdx),
                  MECHA_HUD_ENEMY, MECHA_HUD_EMPTY);
    mecha_hud_rounds(pScrBuf, iWidth, iHeight, pWorld, iTargetIdx,
                     iRightX, 32 * iScale, iScale);
    mecha_hud_reticle(pScrBuf, iWidth, iHeight, pCamera, pWorld, iTargetIdx,
                      iScale);
  }

  /* --- round clock, top centre ------------------------------------------ */

  iSeconds = pWorld->match.iRoundTicks / MECHA_TICK_HZ;
  snprintf(szBuffer, sizeof(szBuffer), "%d", iSeconds);
  mecha_render_text(pScrBuf, iWidth, iHeight,
                    iWidth / 2 - mecha_render_text_width(iScale * 2, szBuffer) / 2,
                    8 * iScale, iScale * 2, MECHA_HUD_TEXT, szBuffer);

  snprintf(szBuffer, sizeof(szBuffer), "ROUND %d", pWorld->match.iRound);
  mecha_render_text(pScrBuf, iWidth, iHeight,
                    iWidth / 2 - mecha_render_text_width(iScale, szBuffer) / 2,
                    26 * iScale, iScale, MECHA_HUD_TEXT, szBuffer);

  /* --- banner ----------------------------------------------------------- */

  szBanner = mecha_phase_banner(pWorld, iViewMech);
  if (szBanner) {
    mecha_render_text(pScrBuf, iWidth, iHeight,
                      iWidth / 2
                        - mecha_render_text_width(iScale * 4, szBanner) / 2,
                      iHeight / 3, iScale * 4, MECHA_HUD_TEXT, szBanner);
  }
}

//-------------------------------------------------------------------------------------------------

void mecha_render_frame(GameRenderer *pRenderer, const tMechaWorld *pWorld,
                        const tMechaCamera *pCamera, int iViewMech,
                        uint8 *pScrBuf, int iWidth, int iHeight,
                        tMechaQuad *paScratch, int iScratchCapacity)
{
  GameRenderCamera cam;
  GameRenderProjection proj;
  float afRight[3];
  float afUp[3];
  float afForward[3];

  if (!pRenderer || !pWorld || !pCamera || !pScrBuf || !paScratch)
    return;
  if (iWidth <= 0 || iHeight <= 0 || iScratchCapacity <= 0)
    return;

  /* The rasteriser reads the destination, its stride and its clip bounds
   * from these globals, exactly as draw_road sets them up for the race. */
  screen_pointer = pScrBuf;
  winx = 0;
  winy = 0;
  winw = iWidth;
  winh = iHeight;
  game_render_set_target(pRenderer, pScrBuf, iWidth, iWidth, iHeight);
  game_render_set_viewport(pRenderer, 0, 0, iWidth, iHeight);

  /* Sky. The arena floor and walls cover everything below the horizon, so a
   * flat fill is the whole background. */
  memset(pScrBuf, pWorld->arena.bySkyPalette,
         (size_t)iWidth * (size_t)iHeight);

  mecha_camera_basis(pCamera, afRight, afUp, afForward);

  memset(&cam, 0, sizeof(cam));
  cam.viewX = pCamera->fX;
  cam.viewY = pCamera->fY;
  cam.viewZ = pCamera->fZ;
  cam.cosYaw = mecha_cos(pCamera->iYaw);
  cam.sinYaw = mecha_sin(pCamera->iYaw);
  cam.fovScale = (float)MECHA_PROJ_VIEWDIST;
  /* The arena is not a track, so there is no chunk to report. */
  cam.renderChunkIdx = -1;
  game_render_set_camera(pRenderer, &cam);

  /* view[worldAxis][viewAxis]: the columns are the camera's own axes written
   * in world space, which is how the rasteriser expects to be handed a
   * basis. */
  memset(&proj, 0, sizeof(proj));
  proj.view[0][0] = afRight[0];   proj.view[0][1] = afUp[0];   proj.view[0][2] = afForward[0];
  proj.view[1][0] = afRight[1];   proj.view[1][1] = afUp[1];   proj.view[1][2] = afForward[1];
  proj.view[2][0] = afRight[2];   proj.view[2][1] = afUp[2];   proj.view[2][2] = afForward[2];
  /* scr_size scales the fixed 320x200 projection frame up to the real
   * buffer; 64 is 1:1, so this is just (width / 320) in sixty-fourths. */
  proj.screenScale = (iWidth * 64) / 320;
  proj.centerX = MECHA_PROJ_CENTRE_X;
  proj.centerY = MECHA_PROJ_CENTRE_Y;
  /* Texture resolution is irrelevant here: every quad in the arena is a flat
   * fill, so nothing ever samples a texture bank. */
  proj.texHalfRes = 0;
  game_render_set_projection(pRenderer, &proj);

  mecha_render_scene(pRenderer, pWorld, pCamera, iViewMech, paScratch,
                     iScratchCapacity);
  mecha_render_hud(pScrBuf, iWidth, iHeight, pWorld, pCamera, iViewMech);
}
