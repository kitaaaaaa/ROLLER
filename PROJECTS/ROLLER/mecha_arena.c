#include "mecha_arena.h"

#include "mecha_defs.h"

#include <math.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------
/*
 * Palette indices.
 *
 * ROLLER's flat polygons take a palette index in the low byte of the surface
 * flags, and that palette comes from the game's own data, so these are tuned
 * numbers rather than derived ones. Every arena and mech colour in the mode
 * goes through a name defined here or in mecha_defs.c, so retuning against a
 * different palette is a one-file edit.
 */
#define MECHA_PAL_FLOOR_A   123
#define MECHA_PAL_FLOOR_B   126
#define MECHA_PAL_GRID      129
#define MECHA_PAL_WALL      120
#define MECHA_PAL_SKY       11
#define MECHA_PAL_BLOCK     124
#define MECHA_PAL_BLOCK_TOP 130
#define MECHA_PAL_HAZARD    193

//-------------------------------------------------------------------------------------------------

#define MECHA_ARENA_COUNT 3

/* Nothing may be pushed further than this in one resolve pass. A mech that
 * somehow ends up deep inside geometry crawls out over a few ticks instead of
 * being flung across the arena. */
#define MECHA_ARENA_MAX_PUSH (6.0f * MECHA_METRE)

//-------------------------------------------------------------------------------------------------

static void mecha_arena_add_box(tMechaArena *pArena,
                                float fX, float fZ,
                                float fHalfX, float fHalfZ,
                                float fHeight,
                                uint8_t byPalette, uint8_t byTrimPalette)
{
  tMechaObstacle *pBox;

  if (pArena->iObstacleCount >= MECHA_MAX_OBSTACLES)
    return;

  pBox = &pArena->aObstacles[pArena->iObstacleCount++];
  pBox->fX = fX;
  pBox->fZ = fZ;
  pBox->fHalfX = fHalfX;
  pBox->fHalfZ = fHalfZ;
  pBox->fHeight = fHeight;
  pBox->byPalette = byPalette;
  pBox->byTrimPalette = byTrimPalette;
  /* Cover is the one thing out here with a real analogue in the retail art,
   * so it takes a building facade and a roof off that bank. Which facade
   * follows the box's own index, so a row of them is not one building
   * repeated. */
  pBox->byTile = (uint8_t)(MECHA_TILE_FACADE_FIRST
                           + (pArena->iObstacleCount
                              % MECHA_TILE_FACADE_COUNT));
  pBox->byTopTile = MECHA_TILE_ROOF;
}

//-------------------------------------------------------------------------------------------------

int mecha_arena_count(void)
{
  return MECHA_ARENA_COUNT;
}

//-------------------------------------------------------------------------------------------------

const char *mecha_arena_name(int iArenaIdx)
{
  static const char *aszNames[MECHA_ARENA_COUNT] = {
    "SECTOR NINE YARD",
    "DRYDOCK PERIMETER",
    "REACTOR DECK",
  };

  if (iArenaIdx < 0)
    iArenaIdx = 0;
  return aszNames[iArenaIdx % MECHA_ARENA_COUNT];
}

//-------------------------------------------------------------------------------------------------

void mecha_arena_init(tMechaArena *pArena, int iArenaIdx)
{
  const float m = MECHA_METRE;

  if (!pArena)
    return;

  memset(pArena, 0, sizeof(*pArena));
  if (iArenaIdx < 0)
    iArenaIdx = 0;
  iArenaIdx %= MECHA_ARENA_COUNT;

  pArena->szName = mecha_arena_name(iArenaIdx);
  pArena->byFloorPalette = MECHA_PAL_FLOOR_A;
  pArena->byGridPalette = MECHA_PAL_GRID;
  pArena->byWallPalette = MECHA_PAL_WALL;

  /*
   * Tiles in the game's own track bank, used whenever the retail data is
   * installed. They do not replace the palette entries above -- those stay
   * as the fallback, so an arena still comes up on a bare checkout and the
   * checkerboard it draws there is the same checkerboard, only flat.
   *
   * Each arena takes a different surface so the three do not read as one
   * place with the furniture moved: a yard in tarmac, a field in grass, and
   * a plate floor in worn metal.
   */
  pArena->byFloorTile = MECHA_TILE_TARMAC_A;
  pArena->byGridTile = MECHA_TILE_TARMAC_B;
  pArena->byWallTile = MECHA_TILE_CONCRETE;

  switch (iArenaIdx) {
  case 0:
    /* Four tall pillars around the middle and four low blocks further out:
     * enough to break a lock at close range without ever letting either mech
     * disappear for long. */
    pArena->fHalfExtent = 110.0f * m;
    pArena->fWallHeight = 22.0f * m;
    mecha_arena_add_box(pArena, -45.0f * m, -45.0f * m, 4.5f * m, 4.5f * m,
                        20.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,  45.0f * m, -45.0f * m, 4.5f * m, 4.5f * m,
                        20.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -45.0f * m,  45.0f * m, 4.5f * m, 4.5f * m,
                        20.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,  45.0f * m,  45.0f * m, 4.5f * m, 4.5f * m,
                        20.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,   0.0f * m, -78.0f * m, 15.0f * m, 5.0f * m,
                        7.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,   0.0f * m,  78.0f * m, 15.0f * m, 5.0f * m,
                        7.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -78.0f * m,   0.0f * m, 5.0f * m, 15.0f * m,
                        7.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,  78.0f * m,   0.0f * m, 5.0f * m, 15.0f * m,
                        7.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    break;

  case 1:
    /* Long, open, and split down the middle by two staggered walls, so the
     * fight is mostly at range and closing costs boost. */
    pArena->fHalfExtent = 140.0f * m;
    pArena->fWallHeight = 26.0f * m;
    pArena->byFloorPalette = MECHA_PAL_FLOOR_B;
    pArena->byFloorTile = MECHA_TILE_GRASS_A;
    pArena->byGridTile = MECHA_TILE_GRASS_B;
    pArena->byWallTile = MECHA_TILE_BRICK;
    mecha_arena_add_box(pArena, -30.0f * m, -20.0f * m, 3.0f * m, 46.0f * m,
                        12.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_HAZARD);
    mecha_arena_add_box(pArena,  30.0f * m,  20.0f * m, 3.0f * m, 46.0f * m,
                        12.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_HAZARD);
    mecha_arena_add_box(pArena, -90.0f * m,  70.0f * m, 9.0f * m, 9.0f * m,
                        9.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,  90.0f * m, -70.0f * m, 9.0f * m, 9.0f * m,
                        9.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,  90.0f * m,  70.0f * m, 9.0f * m, 9.0f * m,
                        9.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -90.0f * m, -70.0f * m, 9.0f * m, 9.0f * m,
                        9.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,   0.0f * m,   0.0f * m, 11.0f * m, 11.0f * m,
                        5.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    break;

  default:
    pArena->byFloorTile = MECHA_TILE_PLATE_A;
    pArena->byGridTile = MECHA_TILE_PLATE_B;
    pArena->byWallTile = MECHA_TILE_RUST;
    /* Small and vertical. The centre block is low enough to jump onto and
     * wide enough to fight on, which turns the whole round into a scrap over
     * high ground. */
    pArena->fHalfExtent = 85.0f * m;
    pArena->fWallHeight = 18.0f * m;
    mecha_arena_add_box(pArena, 0.0f, 0.0f, 20.0f * m, 20.0f * m,
                        10.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_HAZARD);
    mecha_arena_add_box(pArena, -58.0f * m, -58.0f * m, 8.0f * m, 8.0f * m,
                        16.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,  58.0f * m, -58.0f * m, 8.0f * m, 8.0f * m,
                        16.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -58.0f * m,  58.0f * m, 8.0f * m, 8.0f * m,
                        16.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,  58.0f * m,  58.0f * m, 8.0f * m, 8.0f * m,
                        16.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    break;
  }
}

//-------------------------------------------------------------------------------------------------

bool mecha_arena_contains(const tMechaArena *pArena, float fX, float fZ)
{
  if (!pArena)
    return false;
  return fX >= -pArena->fHalfExtent && fX <= pArena->fHalfExtent
      && fZ >= -pArena->fHalfExtent && fZ <= pArena->fHalfExtent;
}

//-------------------------------------------------------------------------------------------------

float mecha_arena_ground_height(const tMechaArena *pArena,
                                float fX, float fZ, float fFeetY)
{
  float fBest = 0.0f;
  int i;

  if (!pArena)
    return 0.0f;

  for (i = 0; i < pArena->iObstacleCount; i++) {
    const tMechaObstacle *pBox = &pArena->aObstacles[i];

    if (fX < pBox->fX - pBox->fHalfX || fX > pBox->fX + pBox->fHalfX)
      continue;
    if (fZ < pBox->fZ - pBox->fHalfZ || fZ > pBox->fZ + pBox->fHalfZ)
      continue;
    /* Below the lip means the box is a wall from here, not a floor. Letting
     * it read as floor is what would teleport a walking mech onto the roof. */
    if (fFeetY < pBox->fHeight - MECHA_ARENA_STEP_UP)
      continue;
    if (pBox->fHeight > fBest)
      fBest = pBox->fHeight;
  }
  return fBest;
}

//-------------------------------------------------------------------------------------------------

static void mecha_arena_push_from_box(const tMechaObstacle *pBox,
                                      float fRadius,
                                      float *pfX, float *pfZ)
{
  float fMinX = pBox->fX - pBox->fHalfX;
  float fMaxX = pBox->fX + pBox->fHalfX;
  float fMinZ = pBox->fZ - pBox->fHalfZ;
  float fMaxZ = pBox->fZ + pBox->fHalfZ;
  float fNearX = mecha_clampf(*pfX, fMinX, fMaxX);
  float fNearZ = mecha_clampf(*pfZ, fMinZ, fMaxZ);
  float fDx = *pfX - fNearX;
  float fDz = *pfZ - fNearZ;
  float fDistSq = fDx * fDx + fDz * fDz;

  if (fDistSq >= fRadius * fRadius)
    return;

  if (fDistSq > 1e-4f) {
    /* Outside the box but overlapping a face or a corner: push straight out
     * along the shortest line to the surface. */
    float fDist = sqrtf(fDistSq);
    float fPush = fRadius - fDist;

    if (fPush > MECHA_ARENA_MAX_PUSH)
      fPush = MECHA_ARENA_MAX_PUSH;
    *pfX += fDx / fDist * fPush;
    *pfZ += fDz / fDist * fPush;
    return;
  }

  /* Dead centre on a face, or genuinely inside. Leave by whichever face is
   * nearest. */
  {
    float fLeft  = (*pfX - fMinX) + fRadius;
    float fRight = (fMaxX - *pfX) + fRadius;
    float fBack  = (*pfZ - fMinZ) + fRadius;
    float fFront = (fMaxZ - *pfZ) + fRadius;
    float fBest = fLeft;
    int iAxis = 0;

    if (fRight < fBest) { fBest = fRight; iAxis = 1; }
    if (fBack  < fBest) { fBest = fBack;  iAxis = 2; }
    if (fFront < fBest) { fBest = fFront; iAxis = 3; }
    if (fBest > MECHA_ARENA_MAX_PUSH)
      fBest = MECHA_ARENA_MAX_PUSH;

    switch (iAxis) {
    case 0: *pfX -= fBest; break;
    case 1: *pfX += fBest; break;
    case 2: *pfZ -= fBest; break;
    default: *pfZ += fBest; break;
    }
  }
}

//-------------------------------------------------------------------------------------------------

bool mecha_arena_resolve_cylinder(const tMechaArena *pArena,
                                  float fRadius, float fFeetY, float fHeight,
                                  float *pfX, float *pfZ)
{
  float fLimit;
  float fStartX;
  float fStartZ;
  int i;

  if (!pArena || !pfX || !pfZ)
    return false;

  fStartX = *pfX;
  fStartZ = *pfZ;

  fLimit = pArena->fHalfExtent - fRadius;
  if (fLimit < 0.0f)
    fLimit = 0.0f;
  *pfX = mecha_clampf(*pfX, -fLimit, fLimit);
  *pfZ = mecha_clampf(*pfZ, -fLimit, fLimit);

  for (i = 0; i < pArena->iObstacleCount; i++) {
    const tMechaObstacle *pBox = &pArena->aObstacles[i];

    /* Standing on the roof, or flying over it, is not a collision. */
    if (fFeetY >= pBox->fHeight - MECHA_ARENA_STEP_UP)
      continue;
    if (fFeetY + fHeight <= 0.0f)
      continue;
    mecha_arena_push_from_box(pBox, fRadius, pfX, pfZ);
  }

  /* A push out of a box can put the mech back through a wall, so the wall
   * clamp gets the last word. */
  *pfX = mecha_clampf(*pfX, -fLimit, fLimit);
  *pfZ = mecha_clampf(*pfZ, -fLimit, fLimit);

  return fabsf(*pfX - fStartX) > 1e-3f || fabsf(*pfZ - fStartZ) > 1e-3f;
}

//-------------------------------------------------------------------------------------------------

static bool mecha_arena_slab(float fStart, float fDelta,
                             float fMin, float fMax,
                             float *pfEnter, float *pfExit)
{
  float fT1;
  float fT2;

  if (fabsf(fDelta) < 1e-6f)
    return fStart >= fMin && fStart <= fMax;

  fT1 = (fMin - fStart) / fDelta;
  fT2 = (fMax - fStart) / fDelta;
  if (fT1 > fT2) {
    float fSwap = fT1;
    fT1 = fT2;
    fT2 = fSwap;
  }
  if (fT1 > *pfEnter)
    *pfEnter = fT1;
  if (fT2 < *pfExit)
    *pfExit = fT2;
  return *pfEnter <= *pfExit;
}

//-------------------------------------------------------------------------------------------------

bool mecha_arena_trace_segment(const tMechaArena *pArena,
                               float fX0, float fY0, float fZ0,
                               float fX1, float fY1, float fZ1,
                               float *pfHitX, float *pfHitY, float *pfHitZ)
{
  float fDx = fX1 - fX0;
  float fDy = fY1 - fY0;
  float fDz = fZ1 - fZ0;
  float fBest = 2.0f;
  int i;

  if (!pArena)
    return false;

  /* Floor. */
  if (fY1 <= 0.0f) {
    if (fY0 <= 0.0f)
      fBest = 0.0f;
    else if (fDy < -1e-6f)
      fBest = fY0 / (fY0 - fY1);
  }

  /* Walls, as four planes. Only an outward crossing counts, so a shot fired
   * from just outside a wall (a mech shoved into the corner) still flies. */
  {
    const float fWall = pArena->fHalfExtent;
    float fT;

    if (fX1 >  fWall && fDx >  1e-6f) { fT = ( fWall - fX0) / fDx; if (fT >= 0.0f && fT < fBest) fBest = fT; }
    if (fX1 < -fWall && fDx < -1e-6f) { fT = (-fWall - fX0) / fDx; if (fT >= 0.0f && fT < fBest) fBest = fT; }
    if (fZ1 >  fWall && fDz >  1e-6f) { fT = ( fWall - fZ0) / fDz; if (fT >= 0.0f && fT < fBest) fBest = fT; }
    if (fZ1 < -fWall && fDz < -1e-6f) { fT = (-fWall - fZ0) / fDz; if (fT >= 0.0f && fT < fBest) fBest = fT; }
  }

  /* Boxes. */
  for (i = 0; i < pArena->iObstacleCount; i++) {
    const tMechaObstacle *pBox = &pArena->aObstacles[i];
    float fEnter = 0.0f;
    float fExit = 1.0f;

    if (!mecha_arena_slab(fX0, fDx, pBox->fX - pBox->fHalfX,
                          pBox->fX + pBox->fHalfX, &fEnter, &fExit))
      continue;
    if (!mecha_arena_slab(fZ0, fDz, pBox->fZ - pBox->fHalfZ,
                          pBox->fZ + pBox->fHalfZ, &fEnter, &fExit))
      continue;
    if (!mecha_arena_slab(fY0, fDy, 0.0f, pBox->fHeight, &fEnter, &fExit))
      continue;
    if (fEnter < 0.0f)
      fEnter = 0.0f;
    if (fEnter <= fExit && fEnter < fBest)
      fBest = fEnter;
  }

  if (fBest > 1.0f)
    return false;

  if (pfHitX) *pfHitX = fX0 + fDx * fBest;
  if (pfHitY) *pfHitY = fY0 + fDy * fBest;
  if (pfHitZ) *pfHitZ = fZ0 + fDz * fBest;
  return true;
}

//-------------------------------------------------------------------------------------------------

void mecha_arena_spawn_point(const tMechaArena *pArena, int iSlot, int iCount,
                             float *pfX, float *pfZ, int *piFacing)
{
  float fRing;
  int iAngle;

  if (!pArena)
    return;
  if (iCount < 1)
    iCount = 1;
  if (iSlot < 0)
    iSlot = 0;

  /* Well inside the walls, and outside the middle where the tall cover
   * usually stands. */
  fRing = pArena->fHalfExtent * 0.62f;
  iAngle = mecha_angle_wrap((MECHA_ANGLE_FULL * (iSlot % iCount)) / iCount);

  if (pfX) *pfX = mecha_sin(iAngle) * fRing;
  if (pfZ) *pfZ = mecha_cos(iAngle) * fRing;
  /* Facing the middle is the opposite heading. */
  if (piFacing) *piFacing = mecha_angle_wrap(iAngle + MECHA_ANGLE_HALF);
}
