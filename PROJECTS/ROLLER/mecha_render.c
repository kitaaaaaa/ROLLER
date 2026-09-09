#include "mecha_render.h"

#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_sim.h"

#include "3d.h"
#include "func2.h"
#include "graphics.h"
#include "roller.h"
#include "scene_render.h"

#include <fcntl.h>
#include <unistd.h>

/* The retail sources open in binary mode explicitly; POSIX has no such flag
 * because it never mangles the bytes. */
#ifndef O_BINARY
#define O_BINARY 0
#endif

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
#define MECHA_HUD_FRAME   115
#define MECHA_HUD_TEXT    143
#define MECHA_HUD_ARMOUR  148
#define MECHA_HUD_ARMOUR_LOW 231
#define MECHA_HUD_BOOST   218
#define MECHA_HUD_BOOST_LOCKED 231
#define MECHA_HUD_AMMO    194
#define MECHA_HUD_EMPTY   119
#define MECHA_HUD_LOCK    183
#define MECHA_HUD_ENEMY   231

/* Camera framing, in metres. */
#define MECHA_CAM_BACK_NEAR  24.0f
#define MECHA_CAM_BACK_FAR   42.0f
/* Roughly two thirds of the way up a machine, which is where an arcade
 * mecha camera sits: high enough to see the floor you are circling on, low
 * enough that the horizon stays in frame and the arena reads as somewhere
 * rather than as a floor plan. */
#define MECHA_CAM_HEIGHT     11.0f

/*
 * Cover is 5 to 20 metres tall, so no fixed low camera clears all of it --
 * and parking the camera above the tallest block is what made the arena read
 * as a floor plan. Instead it sits low and lifts only when something is
 * actually between it and what it is looking at, which is the same segment
 * trace the computer pilot uses to decide whether it has a shot.
 */
#define MECHA_CAM_LIFT_STEP   3.0f
#define MECHA_CAM_LIFT_STEPS  6
#define MECHA_CAM_FLOOR       2.5f

/* The projection reference frame the software rasteriser works in: it
 * projects into a 320x200 space and then scales by scr_size >> 6. */
#define MECHA_PROJ_VIEWDIST 200
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

/* Defined with the effect sprites further down, needed by the quad
 * submission above them. */
static bool mecha_sprites_ensure(GameRenderer *pRenderer);
static bool mecha_sprite_valid(int iFrame);
static TextureHandle s_hSprites;

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

  /* Lift over anything standing in the way. Stepping rather than solving
   * because the trace is cheap and the answer only has to be good enough to
   * see past a box; a camera that slid sideways instead would swing the
   * whole arena around the player for what is usually one pillar. */
  {
    int iStep;

    for (iStep = 0; iStep < MECHA_CAM_LIFT_STEPS; iStep++) {
      if (!mecha_arena_trace_segment(&pWorld->arena,
                                     pCamera->fX, pCamera->fY, pCamera->fZ,
                                     fFocusX, fFocusY, fFocusZ,
                                     NULL, NULL, NULL))
        break;
      pCamera->fY += MECHA_CAM_LIFT_STEP * MECHA_METRE;
    }
  }

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

    /*
     * An effect quad that named a frame gets the game's own animation, when
     * the bank is there to give it one. Everything else -- and everything,
     * when it is not -- rasterises flat, which is why the mesh still picks a
     * palette index for every particle it makes.
     */
    if (pQuad->bySprite >= 0 && mecha_sprites_ensure(pRenderer)
        && mecha_sprite_valid((int)pQuad->bySprite)) {
      /*
       * Which tile of the bank to draw lives in the low byte of the surface
       * type -- it is not a palette index here, which is the one thing about
       * this path that is easy to get wrong: a colour left in those bits
       * names a tile the bank does not have, the renderer rejects it, and
       * the quad quietly comes out flat instead of textured.
       *
       * PARTIAL_TRANS is what makes the frame a sprite rather than a black
       * square. On that path index 0 is skipped instead of written, and
       * every one of these frames is drawn on index 0 -- between a third and
       * nine tenths of each tile is background.
       */
      int iSprite = ((int)pQuad->bySprite & SURFACE_MASK_TEXTURE_INDEX)
                  | SURFACE_FLAG_APPLY_TEXTURE
                  | SURFACE_FLAG_PARTIAL_TRANS;

      /* The legacy path works its own texture coordinates out inside
       * POLYTEX, from the tile index and the projected polygon -- the track
       * renderer passes zeroes on every vertex and always has. Rasterise
       * directly rather than subdividing, for the same reason the flat
       * geometry does: subdivision exists for large perspective surfaces,
       * and a billboard is neither. */
      aVerts[0].u = 0.0f; aVerts[0].v = 0.0f;
      aVerts[1].u = 0.0f; aVerts[1].v = 0.0f;
      aVerts[2].u = 0.0f; aVerts[2].v = 0.0f;
      aVerts[3].u = 0.0f; aVerts[3].v = 0.0f;
      game_render_quad_world(pRenderer, aVerts, s_hSprites, iSprite, 1.0f);
      continue;
    }

    /* A positive threshold below the near plane means every quad rasterises
     * directly instead of being subdivided: subdivision exists for texture
     * perspective, and this geometry is flat. */
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

/*
 * The reticle carries the lock state, because nothing else can. Whether a
 * weapon leads its shot is now the single most important thing on screen,
 * and it is invisible in the world -- so the brackets close up and go hot
 * when the lock is live, open and go amber the moment it starts to slip, and
 * sit wide and cold once it has gone.
 */
static void mecha_hud_reticle(uint8 *pScrBuf, int iWidth, int iHeight,
                              const tMechaCamera *pCamera,
                              const tMechaWorld *pWorld, int iTargetIdx,
                              int iLock, int iScale)
{
  int iScreenX;
  int iScreenY;
  int iArm = 5 * iScale;
  int iGap;
  int iThick = iScale;
  uint8 byColour;

  switch (iLock) {
  case MECHA_LOCK_HELD:
    iGap = 9 * iScale;
    byColour = MECHA_HUD_LOCK;
    break;
  case MECHA_LOCK_SLIPPING:
    iGap = 13 * iScale;
    byColour = MECHA_HUD_AMMO;
    break;
  default:
    iGap = 17 * iScale;
    byColour = MECHA_HUD_EMPTY;
    break;
  }

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
                    iScreenY - iGap, iArm, iThick, byColour);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX - iGap,
                    iScreenY - iGap, iThick, iArm, byColour);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX + iGap - iArm,
                    iScreenY - iGap, iArm, iThick, byColour);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX + iGap - iThick,
                    iScreenY - iGap, iThick, iArm, byColour);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX - iGap,
                    iScreenY + iGap - iThick, iArm, iThick, byColour);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX - iGap,
                    iScreenY + iGap - iArm, iThick, iArm, byColour);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX + iGap - iArm,
                    iScreenY + iGap - iThick, iArm, iThick, byColour);
  mecha_render_fill(pScrBuf, iWidth, iHeight, iScreenX + iGap - iThick,
                    iScreenY + iGap - iArm, iThick, iArm, byColour);
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
  int iBarX;
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

  /*
   * Both machines' condition sits together at the top, player over
   * opponent, which is the arcade convention and it earns its place: the
   * one comparison that decides how you play the next five seconds is
   * whether you are ahead, and that is unreadable when the two bars are in
   * opposite corners of the screen, as they were.
   */
  iBarW = 118 * iScale;
  iBarX = (iWidth - iBarW) / 2;

  /* The clock lives in the bottom right rather than over the bars. It was
   * centred above them, where it sat on top of the player's own name -- and
   * the top of the screen is worth more to the two condition bars than to a
   * number that is only read between exchanges. */
  iSeconds = pWorld->match.iRoundTicks / MECHA_TICK_HZ;
  snprintf(szBuffer, sizeof(szBuffer), "%d", iSeconds);
  mecha_render_text(pScrBuf, iWidth, iHeight,
                    iWidth - mecha_render_text_width(iScale * 2, szBuffer)
                      - 12 * iScale,
                    iHeight - 24 * iScale, iScale * 2, MECHA_HUD_TEXT,
                    szBuffer);

  snprintf(szBuffer, sizeof(szBuffer), "ROUND %d", pWorld->match.iRound);
  mecha_render_text(pScrBuf, iWidth, iHeight,
                    iBarX - mecha_render_text_width(iScale, szBuffer)
                      - 5 * iScale,
                    18 * iScale, iScale, MECHA_HUD_TEXT, szBuffer);

  /* --- the player's own row --------------------------------------------- */

  mecha_render_text(pScrBuf, iWidth, iHeight, iBarX, 10 * iScale, iScale,
                    MECHA_HUD_TEXT, pDef->szName);
  mecha_hud_bar(pScrBuf, iWidth, iHeight, iBarX, 18 * iScale,
                iBarW, 6 * iScale,
                mecha_mech_armour_fraction(pWorld, iViewMech),
                mecha_mech_armour_fraction(pWorld, iViewMech) < 0.3f
                  ? MECHA_HUD_ARMOUR_LOW : MECHA_HUD_ARMOUR,
                MECHA_HUD_EMPTY);
  /* The gauge rides directly under the armour it pays for. It turns red
   * while locked out, which is the one piece of state a player has to be
   * able to read instantly. */
  mecha_hud_bar(pScrBuf, iWidth, iHeight, iBarX, 25 * iScale,
                iBarW, 3 * iScale,
                mecha_mech_boost_fraction(pWorld, iViewMech),
                pMech->bBoostLocked ? MECHA_HUD_BOOST_LOCKED : MECHA_HUD_BOOST,
                MECHA_HUD_EMPTY);
  mecha_hud_rounds(pScrBuf, iWidth, iHeight, pWorld, iViewMech,
                   iBarX + iBarW + 5 * iScale, 18 * iScale, iScale);

  /* --- the opponent, directly below ------------------------------------- */

  iTargetIdx = pMech->iTargetIdx;
  if (iTargetIdx >= 0 && iTargetIdx < MECHA_MAX_MECHS
      && pWorld->aMechs[iTargetIdx].bActive) {
    const tMechaMechDef *pTargetDef =
        mecha_def_get((int)pWorld->aMechs[iTargetIdx].byDefIdx);
    int iNameW = mecha_render_text_width(iScale, pTargetDef->szName);

    mecha_render_text(pScrBuf, iWidth, iHeight, iBarX + iBarW - iNameW,
                      31 * iScale, iScale, MECHA_HUD_ENEMY,
                      pTargetDef->szName);
    mecha_hud_bar(pScrBuf, iWidth, iHeight, iBarX, 39 * iScale,
                  iBarW, 6 * iScale,
                  mecha_mech_armour_fraction(pWorld, iTargetIdx),
                  MECHA_HUD_ENEMY, MECHA_HUD_EMPTY);
    mecha_hud_rounds(pScrBuf, iWidth, iHeight, pWorld, iTargetIdx,
                     iBarX + iBarW + 5 * iScale, 39 * iScale, iScale);
    mecha_hud_reticle(pScrBuf, iWidth, iHeight, pCamera, pWorld, iTargetIdx,
                      (int)pMech->byLock, iScale);
  }

  /* --- what each trigger is holding, along the bottom ------------------- */

  mecha_hud_weapons(pScrBuf, iWidth, iHeight, pWorld, iViewMech,
                    10 * iScale, iHeight - 30 * iScale, iScale);

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
/* Briefing */

/*
 * The briefing's own colours, out of the same table as everything else. The
 * ground is the darkest tone the mode defines, which leaves the two text
 * weights and the selection bar room to separate against it.
 */
#define MECHA_BRIEF_GROUND    MECHA_HUD_FRAME
#define MECHA_BRIEF_BAR       MECHA_HUD_EMPTY
#define MECHA_BRIEF_TITLE     MECHA_HUD_LOCK
#define MECHA_BRIEF_HEADING   MECHA_HUD_AMMO
#define MECHA_BRIEF_DIM       137
#define MECHA_BRIEF_BRIGHT    MECHA_HUD_TEXT
#define MECHA_BRIEF_WIN       MECHA_HUD_ARMOUR
#define MECHA_BRIEF_LOSS      MECHA_HUD_ARMOUR_LOW

/* Keyboard before the slash, pad after it. Kept to the glyphs the font
 * actually has: no plus sign, which is why both triggers reads as BOTH. */
static const char *const s_aaszControls[][2] = {
  { "MOVE",        "W A S D  /  LEFT STICK" },
  { "TURN",        "Q E  /  RIGHT STICK" },
  { "DASH",        "SHIFT  /  B OR LB" },
  { "JUMP",        "SPACE  /  A" },
  { "GUARD",       "C OR CTRL  /  X" },
  { "JUMP CANCEL", "GUARD WHILE AIRBORNE" },
  { "FIRE L C R",  "J K L  /  LT  BOTH  RT" },
  { "CHANGE LOCK", "TAB  /  Y OR RB" },
  { "LEAVE MATCH", "ESC" },
};

#define MECHA_BRIEF_CONTROLS \
  ((int)(sizeof(s_aaszControls) / sizeof(s_aaszControls[0])))

/*
 * Column widths in characters, shared by the controls and the rows so the
 * two blocks line up down the screen. The label column has to clear the
 * longest label on either side of it -- OPPONENT SKILL, at fourteen -- or a
 * setting's value is drawn straight over its own name.
 */
#define MECHA_BRIEF_LABEL_CHARS 16
#define MECHA_BRIEF_VALUE_CHARS 24

/*
 * Lines the screen occupies besides the rows, which vary: two for the
 * double-height title, one for the result, a blank, the controls heading,
 * one per control, a blank either side of the rows, the footer, and one
 * more as the margin the footer's own glyphs need.
 *
 * The budget matters because the game's smaller video mode gives this a
 * 320x200 buffer, and at 200 pixels there is room for exactly twenty-five
 * lines. Anything that does not fit is lost off the bottom, and the bottom
 * is where the exit row lives.
 */
#define MECHA_BRIEF_FIXED_LINES 18

void mecha_render_briefing(const tMechaBriefing *pBrief, uint8 *pScrBuf,
                           int iWidth, int iHeight)
{
  int iScale = iWidth / 320;
  int iLine;
  int iLabelW;
  int iBlockW;
  int iRows;
  int iTotalH;
  int iX;
  int iY;
  int i;

  if (!pBrief || !pScrBuf || iWidth <= 0 || iHeight <= 0)
    return;
  if (iScale < 1)
    iScale = 1;

  iRows = pBrief->iRowCount;
  if (iRows > MECHA_BRIEF_MAX_ROWS)
    iRows = MECHA_BRIEF_MAX_ROWS;
  if (iRows < 0)
    iRows = 0;

  /* Back the scale off until the whole screen fits. A briefing that runs off
   * the bottom loses the exit, which is the one row a player has to be able
   * to find. */
  while (iScale > 1
         && (MECHA_BRIEF_FIXED_LINES + iRows) * (MECHA_GLYPH_H + 1) * iScale
            > iHeight) {
    iScale--;
  }

  iLine = (MECHA_GLYPH_H + 1) * iScale;
  iLabelW = MECHA_BRIEF_LABEL_CHARS * MECHA_GLYPH_ADVANCE * iScale;
  iBlockW = iLabelW + MECHA_BRIEF_VALUE_CHARS * MECHA_GLYPH_ADVANCE * iScale;

  iTotalH = (MECHA_BRIEF_FIXED_LINES + iRows) * iLine;
  iX = (iWidth - iBlockW) / 2;
  iY = (iHeight - iTotalH) / 2;
  if (iX < iScale)
    iX = iScale;
  if (iY < iScale)
    iY = iScale;

  mecha_render_fill(pScrBuf, iWidth, iHeight, 0, 0, iWidth, iHeight,
                    MECHA_BRIEF_GROUND);

  mecha_render_text(pScrBuf, iWidth, iHeight, iX, iY, iScale * 2,
                    MECHA_BRIEF_TITLE, "ARENA");
  iY += iLine * 2;

  /* Only after a match; on the way in there is nothing to report, and the
   * line is left blank rather than closed up so the screen does not shift
   * under the player the first time a match ends. */
  if (pBrief->szResult) {
    mecha_render_text(pScrBuf, iWidth, iHeight, iX, iY, iScale,
                      pBrief->bResultWin ? MECHA_BRIEF_WIN
                                         : MECHA_BRIEF_LOSS,
                      pBrief->szResult);
  }
  iY += iLine * 2;

  mecha_render_text(pScrBuf, iWidth, iHeight, iX, iY, iScale,
                    MECHA_BRIEF_HEADING, "CONTROLS");
  iY += iLine;
  for (i = 0; i < MECHA_BRIEF_CONTROLS; i++) {
    mecha_render_text(pScrBuf, iWidth, iHeight, iX, iY, iScale,
                      MECHA_BRIEF_DIM, s_aaszControls[i][0]);
    mecha_render_text(pScrBuf, iWidth, iHeight, iX + iLabelW, iY, iScale,
                      MECHA_BRIEF_BRIGHT, s_aaszControls[i][1]);
    iY += iLine;
  }
  iY += iLine;

  for (i = 0; i < iRows; i++) {
    const tMechaBriefRow *pRow = &pBrief->aRows[i];
    bool bSelected = i == pBrief->iSelection;

    if (bSelected) {
      /* A bar rather than a cursor glyph: the font has no arrow, and a bar
       * reads at a glance on a screen this dense. */
      mecha_render_fill(pScrBuf, iWidth, iHeight, iX - 3 * iScale,
                        iY - iScale, iBlockW + 6 * iScale,
                        (MECHA_GLYPH_H + 2) * iScale, MECHA_BRIEF_BAR);
    }
    mecha_render_text(pScrBuf, iWidth, iHeight, iX, iY, iScale,
                      bSelected ? MECHA_BRIEF_HEADING : MECHA_BRIEF_DIM,
                      pRow->szLabel ? pRow->szLabel : "");
    if (pRow->szValue) {
      mecha_render_text(pScrBuf, iWidth, iHeight, iX + iLabelW, iY, iScale,
                        MECHA_BRIEF_BRIGHT, pRow->szValue);
    }
    iY += iLine;
  }
  iY += iLine;

  mecha_render_text(pScrBuf, iWidth, iHeight, iX, iY, iScale,
                    MECHA_BRIEF_DIM,
                    "W S CHOOSE   A D CHANGE   ENTER SELECT");
}

//-------------------------------------------------------------------------------------------------
/* Effect sprites */

/*
 * The game's own explosion, flame and smoke frames.
 *
 * They live in the generic texture bank -- gentex.drh -- as 64x64 indexed
 * tiles, and the engine already knows how to decompress that bank and upload
 * it as a 256-pixel-wide atlas. So the mode does not parse anything: it
 * checks the file is there, lets the existing loader do the work, and keeps
 * the handle.
 *
 * The check matters. LoadGenericCarTextures calls ErrorBoxExit when the file
 * is missing, which on a checkout with no retail data would take the process
 * down instead of falling back -- and falling back is the whole point. Every
 * effect still carries a palette index, so a mode with no bank draws exactly
 * what it drew before.
 */
static int s_iSpriteTiles;
static bool s_bSpritesTried;

static bool mecha_sprites_ensure(GameRenderer *pRenderer)
{
  int iFileHandle;

  if (s_hSprites != TEXTURE_HANDLE_INVALID)
    return true;
  if (!pRenderer)
    return false;

  /* Already loaded by the race, in which case it is simply ours to use. */
  if (num_textures[TEXTURE_BANK_CARGEN] > 0) {
    s_hSprites = game_render_get_texture_handle(pRenderer,
                                                TEXTURE_BANK_CARGEN);
    if (s_hSprites != TEXTURE_HANDLE_INVALID) {
      s_iSpriteTiles = num_textures[TEXTURE_BANK_CARGEN];
      return true;
    }
  }

  /* One attempt. A missing bank is not going to appear later in the match,
   * and retrying every frame would stat the filesystem sixty times a
   * second for an answer that cannot change. */
  if (s_bSpritesTried)
    return false;
  s_bSpritesTried = true;

  iFileHandle = ROLLERopen(gencartex_name, O_RDONLY | O_BINARY);
  if (iFileHandle == -1)
    return false;
  close(iFileHandle);

  LoadGenericCarTextures();
  s_iSpriteTiles = num_textures[TEXTURE_BANK_CARGEN];
  s_hSprites = game_render_get_texture_handle(pRenderer, TEXTURE_BANK_CARGEN);

  /*
   * The loader uploads through g_pGameRenderer, the global the race sets up.
   * This mode may be running on a renderer that global has never pointed at
   * -- the headless test builds its own, and the mode itself stands one up
   * when it is entered before any race -- in which case the decompress and
   * the sort happened but the upload was skipped, and the handle comes back
   * invalid with the tiles counted. The pixels are sitting in cargen_vga
   * either way, so hand them to the renderer that is actually drawing.
   */
  if (s_hSprites == TEXTURE_HANDLE_INVALID && cargen_vga
      && s_iSpriteTiles > 0) {
    int iTile = gfx_size ? 32 : 64;
    int iPerRow = 256 / iTile;
    int iRows = (s_iSpriteTiles + iPerRow - 1) / iPerRow;

    s_hSprites = game_render_load_texture(pRenderer, cargen_vga, 256,
                                          iRows * iTile,
                                          TEXTURE_BANK_CARGEN, gfx_size);
  }
  return s_hSprites != TEXTURE_HANDLE_INVALID && s_iSpriteTiles > 0;
}

bool mecha_render_sprites_active(void)
{
  return s_hSprites != TEXTURE_HANDLE_INVALID && s_iSpriteTiles > 0;
}

/* True when the loaded bank actually has this frame. */
static bool mecha_sprite_valid(int iFrame)
{
  return iFrame >= 0 && iFrame < s_iSpriteTiles;
}

//-------------------------------------------------------------------------------------------------
/* Sky */

/*
 * The sky, top band first, running down to the one that sits on the horizon.
 * A flat fill was what the arena had, and against a horizon this low it read
 * as a wall rather than as distance -- the whole depth of an outdoor arena is
 * carried by the sky, because everything else on screen is untextured flat
 * shading.
 */
static const uint8 s_abySkyBand[] = {
  221, 224, 227, 230, 167, 170, 171, 204, 207
};

#define MECHA_SKY_BANDS \
  ((int)(sizeof(s_abySkyBand) / sizeof(s_abySkyBand[0])))

/* Below the horizon. The floor covers most of it, but not the gap past the
 * arena wall, and sky colour showing under the ground reads as a hole. */
#define MECHA_SKY_GROUND 118

/*
 * Which row the horizon falls on.
 *
 * Taken through the same projection the rasteriser uses rather than guessed
 * at, so the gradient stays welded to the world when the camera pitches: for
 * a ray that is horizontal in world space and infinitely far off, the
 * view-space slope works out as -tan(pitch), which puts the horizon at
 * 99 + viewdist * tan(pitch) in the 320x200 reference frame.
 */
static int mecha_horizon_row(const tMechaCamera *pCamera, int iHeight)
{
  float fSin = mecha_sin(pCamera->iPitch);
  float fCos = mecha_cos(pCamera->iPitch);
  float fRefY;

  /* Straight up or straight down: the horizon is off the frame either way,
   * and the clamps below put it there. */
  if (fCos > -1e-3f && fCos < 1e-3f)
    return fSin >= 0.0f ? iHeight : 0;

  fRefY = (199.0f - (float)MECHA_PROJ_CENTRE_Y)
        + (float)MECHA_PROJ_VIEWDIST * (fSin / fCos);
  fRefY = fRefY * (float)iHeight / 200.0f;

  if (fRefY < 0.0f)
    return 0;
  if (fRefY > (float)iHeight)
    return iHeight;
  return (int)fRefY;
}

static void mecha_render_sky(uint8 *pScrBuf, int iWidth, int iHeight,
                             const tMechaCamera *pCamera)
{
  int iHorizon = mecha_horizon_row(pCamera, iHeight);
  int y;

  for (y = 0; y < iHeight; y++) {
    uint8 byColour = MECHA_SKY_GROUND;

    if (y < iHorizon) {
      int iBand = y * MECHA_SKY_BANDS / iHorizon;

      if (iBand >= MECHA_SKY_BANDS)
        iBand = MECHA_SKY_BANDS - 1;
      byColour = s_abySkyBand[iBand];
    }
    memset(pScrBuf + (size_t)y * (size_t)iWidth, byColour, (size_t)iWidth);
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

  mecha_render_sky(pScrBuf, iWidth, iHeight, pCamera);

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

//-------------------------------------------------------------------------------------------------
/* Palette */

/*
 * Every index the arena mode paints with, and the colour it means.
 *
 * The names live next to the code that uses them (mecha_arena.c,
 * mecha_defs.c, mecha_mesh.c and the HUD block above); this is where those
 * indices get colours for the case where no palette has been loaded at all.
 *
 * The indices themselves were chosen by matching these intended colours
 * against the retail palette, so that a player who has the game data sees
 * roughly the same picture from their own PALETTE.PAL rather than whatever
 * happened to sit at an arbitrary index. That palette is mostly a grey ramp
 * between 115 and 143 with saturated primaries higher up, which is why the
 * arena reads as grey structure with coloured tracers. Some indices are
 * deliberately shared -- a tracer and a HUD accent -- because the mode is
 * painting into a palette it does not own the whole of.
 */
static const struct
{
  uint8 byIndex;
  uint8 byR;
  uint8 byG;
  uint8 byB;
} s_aArenaPalette[] = {
  {  11,  8, 10, 26 },   /* sky                                         */
  {  18, 45, 39, 30 },   /* iron trim                                   */
  {  35, 44, 20, 60 },   /* violet tracer                               */
  {  67, 58, 52, 30 },   /* sand tracer                                 */
  { 105, 22, 22, 25 },   /* mech joints                                 */
  { 115,  5,  5,  7 },   /* HUD frame, the darkest tone used            */
  { 119, 13, 13, 17 },   /* dark hull, empty HUD socket                 */
  { 120, 15, 15, 17 },   /* arena wall                                  */
  { 123, 20, 20, 22 },   /* floor tile A                                */
  { 124, 29, 25, 20 },   /* arena block                                 */
  { 125, 31, 26, 21 },   /* iron hull                                   */
  { 126, 27, 27, 29 },   /* floor tile B                                */
  { 127, 30, 30, 35 },   /* dark trim                                   */
  { 128, 30, 32, 37 },   /* steel hull                                  */
  { 129, 33, 33, 35 },   /* floor grid tile                             */
  { 130, 41, 37, 30 },   /* block top                                   */
  { 136, 45, 47, 51 },   /* steel trim                                  */
  { 137, 47, 49, 53 },   /* pale hull                                   */
  { 141, 58, 58, 61 },   /* pale trim                                   */
  { 143, 63, 63, 63 },   /* white tracer, HUD text                      */
  { 148, 16, 60, 24 },   /* green tracer, armour bar                    */
  { 183, 63, 40,  8 },   /* orange tracer, lock reticle                 */
  { 193, 60, 45, 10 },   /* hazard trim                                 */
  { 194, 63, 52, 10 },   /* amber tracer, ammo pips                     */
  { 218, 16, 52, 63 },   /* cyan tracer, boost gauge                    */
  { 231, 63, 12, 12 },   /* red tracer, low armour, enemy bar           */

  /*
   * The sky, deepest first. These indices are not arbitrary: in the game's
   * own PALETTE.PAL 221-230 is a dark-to-bright red ramp, 167-171 a
   * dark-to-bright orange one and 204-207 the top of a yellow one, so the
   * band list below climbs steadily in brightness whether it is resolved
   * through the retail palette or through this table. 231 was the obvious
   * brightest red to finish the reds on and is deliberately not used: it is
   * the low-armour warning, and a sky that matches the colour of "you are
   * about to die" is a sky that hides it.
   */
  { 221, 15,  3, 10 },   /* zenith                                      */
  { 224, 27,  4,  9 },
  { 227, 39,  6,  8 },
  { 230, 51,  9,  6 },
  { 167, 57, 20,  5 },
  { 170, 61, 30,  6 },
  { 171, 63, 38,  8 },
  { 204, 63, 48, 12 },
  { 207, 63, 58, 22 },   /* the band sitting on the horizon             */
  { 118,  9,  8, 10 },   /* everything below it                         */
};

#define MECHA_PALETTE_COUNT \
  ((int)(sizeof(s_aArenaPalette) / sizeof(s_aArenaPalette[0])))

//-------------------------------------------------------------------------------------------------

void mecha_render_build_palette(tColor *paPalette)
{
  int i;

  if (!paPalette)
    return;

  /* Anything the mode never paints with gets one neutral dark tone. Leaving
   * it black would make an unnoticed index look like a hole in the world;
   * this way a stray colour reads as a flat grey and is obvious. */
  for (i = 0; i < 256; i++) {
    paPalette[i].byR = 6;
    paPalette[i].byG = 6;
    paPalette[i].byB = 7;
  }

  for (i = 0; i < MECHA_PALETTE_COUNT; i++) {
    tColor *pEntry = &paPalette[s_aArenaPalette[i].byIndex];

    pEntry->byR = s_aArenaPalette[i].byR;
    pEntry->byG = s_aArenaPalette[i].byG;
    pEntry->byB = s_aArenaPalette[i].byB;
  }
}

//-------------------------------------------------------------------------------------------------

bool mecha_render_palette_defines(int iIndex)
{
  int i;

  for (i = 0; i < MECHA_PALETTE_COUNT; i++) {
    if ((int)s_aArenaPalette[i].byIndex == iIndex)
      return true;
  }
  return false;
}
