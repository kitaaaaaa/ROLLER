#include "mecha_arena.h"

#include "mecha_defs.h"

#include <math.h>
#include <string.h>

//-------------------------------------------------------------------------------------------------
/*
 * Palette indices: tuned against the game's own palette rather than derived.
 * Every arena and mech colour goes through a name here or in mecha_defs.c,
 * so retuning is a one-file edit. [ARENA-01]
 */
#define MECHA_PAL_FLOOR_A   123
#define MECHA_PAL_FLOOR_B   126
#define MECHA_PAL_GRID      129
#define MECHA_PAL_WALL      120
#define MECHA_PAL_SKY       11
#define MECHA_PAL_BLOCK     124
#define MECHA_PAL_BLOCK_TOP 130
#define MECHA_PAL_HAZARD    193
/* The green arena, off the palette's own green and brown ramps rather than
 * borrowed tracer colours. [ARENA-01] */
#define MECHA_PAL_GRASS_A   249
#define MECHA_PAL_GRASS_B   246
#define MECHA_PAL_BARK      57
#define MECHA_PAL_LEAF      252
#define MECHA_PAL_ROCK      125
#define MECHA_PAL_ROCK_TOP  130

//-------------------------------------------------------------------------------------------------
/*
 * Marching a shot across the ground. The stride decides whether a bullet can
 * step over a hillside between samples; the cap only stops an absurdly long
 * query becoming an unbounded loop. [ARENA-02]
 */
#define MECHA_TRACE_STRIDE  MECHA_M(1.0f)
#define MECHA_TRACE_STEPS   48
#define MECHA_TRACE_BISECT  12
/*
 * How far under the surface counts as still being above it. A muzzle can
 * graze a slope by a few centimetres, and a shot that begins underground
 * detonates at the muzzle. [ARENA-03]
 */
#define MECHA_TRACE_SKIN    MECHA_M(0.2f)

//-------------------------------------------------------------------------------------------------

#define MECHA_ARENA_COUNT 7

/* Nothing may be pushed further than this in one resolve pass. A mech that
 * somehow ends up deep inside geometry crawls out over a few ticks instead of
 * being flung across the arena. */
#define MECHA_ARENA_MAX_PUSH (6.0f * MECHA_METRE)

//-------------------------------------------------------------------------------------------------

/*
 * Terrain helpers. A hill is raised by hand rather than by noise, and comes
 * out as a dozen facets, which is what it should look like. [ARENA-04]
 */
/* How much of a hill's reach is its flat top. A third leaves sides steep
 * enough to be a ramp and a top wide enough for two machines to argue on. */
#define MECHA_HILL_FLAT_TOP 0.34f

/* How many cells this arena's ground is divided into, whatever it asked
 * for, clamped to what the arrays can hold. */
static int mecha_arena_cells(const tMechaArena *pArena)
{
  int iCells = pArena->iTerrainCells > 0 ? pArena->iTerrainCells
                                         : MECHA_TERRAIN_CELLS_DEFAULT;

  if (iCells > MECHA_TERRAIN_CELLS)
    iCells = MECHA_TERRAIN_CELLS;
  return iCells;
}

//-------------------------------------------------------------------------------------------------

static void mecha_arena_raise(tMechaArena *pArena, float fX, float fZ,
                              float fReach, float fHeight)
{
  int iCells = mecha_arena_cells(pArena);
  float fCell = pArena->fHalfExtent * 2.0f / (float)iCells;
  int iRow;
  int iCol;

  if (fReach <= 0.0f || fCell <= 0.0f)
    return;
  for (iRow = 0; iRow <= iCells; iRow++) {
    for (iCol = 0; iCol <= iCells; iCol++) {
      float fNodeX = -pArena->fHalfExtent + fCell * (float)iCol;
      float fNodeZ = -pArena->fHalfExtent + fCell * (float)iRow;
      float fAway = mecha_length2(fNodeX - fX, fNodeZ - fZ);
      float fLift;

      if (fAway >= fReach)
        continue;
      /*
       * A truncated cone: constant-grade sides to launch off, a flat top to
       * land and fight on, and a definite lip between them. [ARENA-05]
       */
      fLift = fAway <= fReach * MECHA_HILL_FLAT_TOP
              ? fHeight
              : fHeight * (fReach - fAway)
                / (fReach * (1.0f - MECHA_HILL_FLAT_TOP));
      if (fLift > pArena->afNode[iRow][iCol])
        pArena->afNode[iRow][iCol] = fLift;
    }
  }
}

//-------------------------------------------------------------------------------------------------

/* Marks every cell whose middle falls inside the reach. Cells, not corners:
 * a surface belongs to a piece of ground, the way a track's does. */
/*
 * Everything falls away. The ground an arena actually has is then painted
 * back over it a piece at a time, which is how ground that is not a filled
 * square gets built at all.
 *
 * The drop is not a pit flag, deliberately. A pit kills a machine the moment
 * its feet are on one, which reads as being deleted rather than as falling.
 * Ground far enough below the kill plane makes the machine fall, tumble and
 * expire on the way down, which is what happens off the side of any open
 * arena and is what a hole should feel like. [ARENA-15]
 */
static void mecha_arena_void(tMechaArena *pArena, float fDepth)
{
  int iCells = mecha_arena_cells(pArena);
  int iRow;
  int iCol;

  for (iRow = 0; iRow <= iCells; iRow++)
    for (iCol = 0; iCol <= iCells; iCol++)
      pArena->afNode[iRow][iCol] = -fDepth;
}

//-------------------------------------------------------------------------------------------------

/*
 * A length of lane. The centre, the width and the height are all given at
 * both ends and interpolated between, so a run of these follows a curve
 * instead of stepping along it -- which is what a causeway measured off a
 * real map needs. Anything off the lane is left as it was, so two lanes
 * painted apart from each other leave a hole between them without anybody
 * carving one. [ARENA-15]
 */
static void mecha_arena_lane(tMechaArena *pArena, float fX0, float fX1,
                             float fZ0, float fHalf0, float fY0,
                             float fZ1, float fHalf1, float fY1)
{
  int iCells = mecha_arena_cells(pArena);
  float fCell = pArena->fHalfExtent * 2.0f / (float)iCells;
  float fSpan = fX1 - fX0;
  int iRow;
  int iCol;

  if (fCell <= 0.0f || fabsf(fSpan) < 1e-3f)
    return;
  for (iCol = 0; iCol <= iCells; iCol++) {
    float fNodeX = -pArena->fHalfExtent + fCell * (float)iCol;
    float fT = (fNodeX - fX0) / fSpan;
    float fZ;
    float fHalf;
    float fY;

    if (fT < 0.0f || fT > 1.0f)
      continue;
    fZ = fZ0 + (fZ1 - fZ0) * fT;
    fHalf = fHalf0 + (fHalf1 - fHalf0) * fT;
    fY = fY0 + (fY1 - fY0) * fT;
    for (iRow = 0; iRow <= iCells; iRow++) {
      float fNodeZ = -pArena->fHalfExtent + fCell * (float)iRow;

      if (fabsf(fNodeZ - fZ) <= fHalf)
        pArena->afNode[iRow][iCol] = fY;
    }
  }
}

//-------------------------------------------------------------------------------------------------

/* One more station on the way being built. Silently full is better than
 * half a way: a chain that stops in the middle of a causeway leads pilots
 * to the edge of it. [AI-13] */
static void mecha_arena_way_point(tMechaWay *pWay, float fX, float fZ,
                                  float fHalf)
{
  if (!pWay || pWay->iCount >= MECHA_WAY_POINTS)
    return;
  pWay->aPoints[pWay->iCount].fX = fX;
  pWay->aPoints[pWay->iCount].fZ = fZ;
  pWay->aPoints[pWay->iCount].fHalf = fHalf;
  pWay->iCount++;
}

//-------------------------------------------------------------------------------------------------

/*
 * The last box added is masonry rather than a glazed facade. Cover defaults
 * to the retail building art, which is right for a city and wrong for a
 * keep -- and MECHA_TILE_CONCRETE is no help, because that index is one of
 * the facades. Brick is the one masonry face the bank has.
 */
static void mecha_arena_face_stone(tMechaArena *pArena)
{
  tMechaObstacle *pBox;

  if (pArena->iObstacleCount <= 0)
    return;
  pBox = &pArena->aObstacles[pArena->iObstacleCount - 1];
  pBox->byTile = MECHA_TILE_RUST;
  pBox->byTopTile = MECHA_TILE_PLATE_B;
}

//-------------------------------------------------------------------------------------------------

/* A flat square of ground, for what stands at the ends of a causeway. */
static void mecha_arena_pad(tMechaArena *pArena, float fX, float fZ,
                            float fHalfX, float fHalfZ, float fY)
{
  int iCells = mecha_arena_cells(pArena);
  float fCell = pArena->fHalfExtent * 2.0f / (float)iCells;
  int iRow;
  int iCol;

  if (fCell <= 0.0f)
    return;
  for (iRow = 0; iRow <= iCells; iRow++) {
    for (iCol = 0; iCol <= iCells; iCol++) {
      float fNodeX = -pArena->fHalfExtent + fCell * (float)iCol;
      float fNodeZ = -pArena->fHalfExtent + fCell * (float)iRow;

      if (fabsf(fNodeX - fX) > fHalfX || fabsf(fNodeZ - fZ) > fHalfZ)
        continue;
      pArena->afNode[iRow][iCol] = fY;
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_arena_mark(tMechaArena *pArena, float fX, float fZ,
                             float fReach, uint32_t uiFlags)
{
  int iCells = mecha_arena_cells(pArena);
  float fCell = pArena->fHalfExtent * 2.0f / (float)iCells;
  int iRow;
  int iCol;

  if (fCell <= 0.0f)
    return;
  for (iRow = 0; iRow < iCells; iRow++) {
    for (iCol = 0; iCol < iCells; iCol++) {
      float fMidX = -pArena->fHalfExtent + fCell * ((float)iCol + 0.5f);
      float fMidZ = -pArena->fHalfExtent + fCell * ((float)iRow + 0.5f);

      if (mecha_length2(fMidX - fX, fMidZ - fZ) <= fReach)
        pArena->auiSurface[iRow][iCol] |= uiFlags;
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void mecha_arena_mark_all(tMechaArena *pArena, uint32_t uiFlags)
{
  int iCells = mecha_arena_cells(pArena);
  int iRow;
  int iCol;

  for (iRow = 0; iRow < iCells; iRow++)
    for (iCol = 0; iCol < iCells; iCol++)
      pArena->auiSurface[iRow][iCol] |= uiFlags;
}

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
  pBox->byKind = MECHA_PROP_BLOCK;
}

//-------------------------------------------------------------------------------------------------

int mecha_arena_count(void)
{
  return MECHA_ARENA_COUNT;
}

//-------------------------------------------------------------------------------------------------

/* Cover that grew there. Same box to walk into, different thing drawn round
 * it, and it stands on the ground wherever the ground happens to be. */
static void mecha_arena_add_prop(tMechaArena *pArena, uint8_t byKind,
                                 float fX, float fZ, float fHalf,
                                 float fHeight)
{
  tMechaObstacle *pBox;

  if (pArena->iObstacleCount >= MECHA_MAX_OBSTACLES)
    return;
  mecha_arena_add_box(pArena, fX, fZ, fHalf, fHalf, fHeight,
                      byKind == MECHA_PROP_TREE ? MECHA_PAL_BARK
                                                : MECHA_PAL_ROCK,
                      byKind == MECHA_PROP_TREE ? MECHA_PAL_LEAF
                                                : MECHA_PAL_ROCK_TOP);
  pBox = &pArena->aObstacles[pArena->iObstacleCount - 1];
  pBox->byKind = byKind;
  pBox->byTile = byKind == MECHA_PROP_TREE ? MECHA_TILE_GRASS_A
                                           : MECHA_TILE_CONCRETE;
  pBox->byTopTile = byKind == MECHA_PROP_TREE ? MECHA_TILE_GRASS_B
                                              : MECHA_TILE_PLATE_A;
}

//-------------------------------------------------------------------------------------------------

const char *mecha_arena_name(int iArenaIdx)
{
  static const char *aszNames[MECHA_ARENA_COUNT] = {
    "SECTOR NINE YARD",
    "DRYDOCK PERIMETER",
    "REACTOR DECK",
    "COLDWATER MEADOW",
    "TOWER SEVEN ROOF",
    "MERIDIAN CROSSING",
    "FACING WORLDS",
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
   * Track-bank tiles when the retail data is installed; the palette entries
   * above stay as the fallback. A different surface per arena, so they do
   * not read as one place with the furniture moved. [ARENA-06]
   */
  pArena->byFloorTile = MECHA_TILE_TARMAC_A;
  pArena->byGridTile = MECHA_TILE_TARMAC_B;
  pArena->byWallTile = MECHA_TILE_CONCRETE;

  switch (iArenaIdx) {
  case 0:
    /* Four tall pillars around the middle and four low blocks further out:
     * enough to break a lock at close range without ever letting either mech
     * disappear for long. */
    pArena->fHalfExtent = 130.0f * m;
    pArena->fWallHeight = 22.0f * m;
    mecha_arena_add_box(pArena, -53.2f * m, -53.2f * m, 4.5f * m, 4.5f * m,
                        20.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 53.2f * m, -53.2f * m, 4.5f * m, 4.5f * m,
                        20.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -53.2f * m, 53.2f * m, 4.5f * m, 4.5f * m,
                        20.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 53.2f * m, 53.2f * m, 4.5f * m, 4.5f * m,
                        20.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 0.0f * m, -92.2f * m, 15.0f * m, 5.0f * m,
                        7.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 0.0f * m, 92.2f * m, 15.0f * m, 5.0f * m,
                        7.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -92.2f * m, 0.0f * m, 5.0f * m, 15.0f * m,
                        7.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 92.2f * m, 0.0f * m, 5.0f * m, 15.0f * m,
                        7.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    break;

  case 1:
    /* Long, open, and split down the middle by two staggered walls, so the
     * fight is mostly at range and closing costs boost. */
    pArena->fHalfExtent = 165.0f * m;
    pArena->fWallHeight = 26.0f * m;
    pArena->byFloorPalette = MECHA_PAL_FLOOR_B;
    pArena->byFloorTile = MECHA_TILE_GRASS_A;
    pArena->byGridTile = MECHA_TILE_GRASS_B;
    pArena->byWallTile = MECHA_TILE_BRICK;
    mecha_arena_add_box(pArena, -35.4f * m, -23.6f * m, 3.0f * m, 46.0f * m,
                        12.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_HAZARD);
    mecha_arena_add_box(pArena, 35.4f * m, 23.6f * m, 3.0f * m, 46.0f * m,
                        12.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_HAZARD);
    mecha_arena_add_box(pArena, -106.1f * m, 82.5f * m, 9.0f * m, 9.0f * m,
                        9.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 106.1f * m, -82.5f * m, 9.0f * m, 9.0f * m,
                        9.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 106.1f * m, 82.5f * m, 9.0f * m, 9.0f * m,
                        9.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -106.1f * m, -82.5f * m, 9.0f * m, 9.0f * m,
                        9.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 0.0f * m, 0.0f * m, 11.0f * m, 11.0f * m,
                        5.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    break;

  case 3: {
    /*
     * Open country: eight sides, non-magnetic hills you can be thrown off,
     * and no walls you can see -- what is drawn past the boundary is more
     * forest. [ARENA-07]
     */
    int iTree;

    pArena->byShape = MECHA_ARENA_OCTAGON;
    pArena->fHalfExtent = 305.0f * m;
    pArena->fWallHeight = 0.0f;
    pArena->iFloorTiles = 47;
    /* Twice the arena, twice the grid: the hills are the same size they
     * always were, and a grid stretched to cover twice the ground would
     * have rounded them off into bumps. */
    pArena->iTerrainCells = 28;
    pArena->fOuterReach = 640.0f * m;
    pArena->iBillboards = 420;
    pArena->byFloorPalette = MECHA_PAL_GRASS_A;
    pArena->byGridPalette = MECHA_PAL_GRASS_B;
    pArena->byFloorTile = MECHA_TILE_GRASS_A;
    pArena->byGridTile = MECHA_TILE_GRASS_B;
    pArena->byWallTile = MECHA_TILE_BRICK;
    pArena->fKillY = -60.0f * m;

    /*
     * Tight enough to cross inside one burst, which is what makes them
     * ramps: a hill a hundred metres across is a walk up whatever you do,
     * because the boost has run out before the crest.
     */
    mecha_arena_raise(pArena, -110.0f * m, -80.0f * m, 44.0f * m, 22.0f * m);
    mecha_arena_raise(pArena,  124.0f * m,  60.0f * m, 50.0f * m, 26.0f * m);
    mecha_arena_raise(pArena,   20.0f * m, -170.0f * m, 38.0f * m, 16.0f * m);
    mecha_arena_raise(pArena, -160.0f * m,  150.0f * m, 36.0f * m, 15.0f * m);
    mecha_arena_raise(pArena,   30.0f * m,  120.0f * m, 46.0f * m, 24.0f * m);
    mecha_arena_raise(pArena, -190.0f * m,  -20.0f * m, 40.0f * m, 18.0f * m);
    mecha_arena_raise(pArena,  170.0f * m, -140.0f * m, 42.0f * m, 20.0f * m);
    mecha_arena_mark_all(pArena, MECHA_SURF_NON_MAGNETIC);

    for (iTree = 0; iTree < 14; iTree++) {
      /* Spread round a ring and then pushed about, so it is a wood rather
       * than an orchard. These are the ones you can hide behind; the rest
       * of the forest is scenery, drawn but not there. */
      int iAngle = iTree * MECHA_ANGLE_FULL / 14;
      float fReach = (96.0f + 26.0f * (float)(iTree % 4)) * m;

      mecha_arena_add_prop(pArena, MECHA_PROP_TREE,
                           mecha_sin(iAngle) * fReach,
                           mecha_cos(iAngle) * fReach,
                           (5.0f + (float)(iTree % 3)) * m,
                           (26.0f + 4.0f * (float)(iTree % 3)) * m);
    }
    mecha_arena_add_prop(pArena, MECHA_PROP_ROCK, -36.0f * m, 32.0f * m,
                         12.0f * m, 11.0f * m);
    mecha_arena_add_prop(pArena, MECHA_PROP_ROCK, 68.0f * m, -104.0f * m,
                         9.0f * m, 8.0f * m);
    mecha_arena_add_prop(pArena, MECHA_PROP_ROCK, -148.0f * m, -16.0f * m,
                         10.0f * m, 9.0f * m);
    mecha_arena_add_prop(pArena, MECHA_PROP_ROCK, 150.0f * m, 96.0f * m,
                         11.0f * m, 10.0f * m);
    break;
  }

  case 5: {
    /*
     * Nine city blocks in the middle of open country, on the biggest ground
     * the mode has. The streets run past the last building all the way to
     * the boundary. [ARENA-08]
     */
    int iTree;
    int iRow;
    int iCol;
    /* Block pitch, and the buildings inside it. The gap between the two is
     * the street, and it is wide enough for two machines to pass without
     * either of them being cover for the other. */
    const float fPitch = 96.0f * m;
    const float fBlock = 31.0f * m;
    const float fStreet = 17.0f * m;

    pArena->byShape = MECHA_ARENA_OCTAGON;
    pArena->fHalfExtent = 420.0f * m;
    pArena->fWallHeight = 0.0f;
    pArena->iFloorTiles = 46;
    pArena->iTerrainCells = 24;
    pArena->fOuterReach = 760.0f * m;
    pArena->iBillboards = 460;
    pArena->byFloorPalette = MECHA_PAL_GRASS_A;
    pArena->byGridPalette = MECHA_PAL_GRASS_B;
    pArena->byFloorTile = MECHA_TILE_GRASS_A;
    pArena->byGridTile = MECHA_TILE_GRASS_B;
    pArena->byWallTile = MECHA_TILE_BRICK;
    pArena->fKillY = -60.0f * m;

    /*
     * Hills, kept out of the middle: a city built on a slope would have the
     * streets climbing, and the streets are the one flat thing here.
     */
    mecha_arena_raise(pArena, -250.0f * m, -140.0f * m, 54.0f * m, 24.0f * m);
    mecha_arena_raise(pArena,  240.0f * m,  180.0f * m, 58.0f * m, 27.0f * m);
    mecha_arena_raise(pArena,   60.0f * m, -290.0f * m, 46.0f * m, 18.0f * m);
    mecha_arena_raise(pArena, -300.0f * m,  190.0f * m, 44.0f * m, 17.0f * m);
    mecha_arena_raise(pArena,  300.0f * m, -230.0f * m, 50.0f * m, 21.0f * m);
    mecha_arena_raise(pArena, -110.0f * m,  280.0f * m, 48.0f * m, 20.0f * m);
    mecha_arena_mark_all(pArena, MECHA_SURF_NON_MAGNETIC);

    /*
     * The street grid: four lines each way, the two that bound the city and
     * the two that run between its blocks, each marked from one side of the
     * arena clean through to the other. Marking is by cell, so a line is
     * laid as a row of overlapping stamps down its length.
     */
    for (iRow = 0; iRow < 4; iRow++) {
      float fLine = (-1.5f + (float)iRow) * fPitch;
      int iStep;

      for (iStep = 0; iStep <= 60; iStep++) {
        float fAlong = (-1.0f + (float)iStep / 30.0f) * pArena->fHalfExtent;

        mecha_arena_mark(pArena, fAlong, fLine, fStreet, MECHA_SURF_ROAD);
        mecha_arena_mark(pArena, fLine, fAlong, fStreet, MECHA_SURF_ROAD);
      }
    }

    /* And the nine blocks, tall enough that the streets between them are
     * corridors rather than gaps. Alternating heights so the skyline is a
     * skyline. */
    for (iRow = 0; iRow < 3; iRow++)
      for (iCol = 0; iCol < 3; iCol++) {
        static const float afStorey[9] = {
          78.0f, 54.0f, 92.0f, 62.0f, 104.0f, 70.0f, 86.0f, 58.0f, 74.0f
        };

        mecha_arena_add_box(pArena,
                            (-1.0f + (float)iCol) * fPitch,
                            (-1.0f + (float)iRow) * fPitch,
                            fBlock, fBlock,
                            afStorey[iRow * 3 + iCol] * m,
                            MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
      }

    /* Country, out past the last of it. */
    for (iTree = 0; iTree < 8; iTree++) {
      int iAngle = iTree * MECHA_ANGLE_FULL / 8 + MECHA_ANGLE_FULL / 24;
      float fReach = (250.0f + 40.0f * (float)(iTree % 3)) * m;

      mecha_arena_add_prop(pArena, MECHA_PROP_TREE,
                           mecha_sin(iAngle) * fReach,
                           mecha_cos(iAngle) * fReach,
                           (5.0f + (float)(iTree % 3)) * m,
                           (28.0f + 4.0f * (float)(iTree % 3)) * m);
    }
    mecha_arena_add_prop(pArena, MECHA_PROP_ROCK, -196.0f * m, 60.0f * m,
                         12.0f * m, 11.0f * m);
    mecha_arena_add_prop(pArena, MECHA_PROP_ROCK, 176.0f * m, -188.0f * m,
                         10.0f * m, 9.0f * m);
    mecha_arena_add_prop(pArena, MECHA_PROP_ROCK, 40.0f * m, 214.0f * m,
                         11.0f * m, 10.0f * m);
    break;
  }

  case 6: {
    /*
     * Two keeps at the ends of a causeway with nothing either side of it,
     * after the Unreal Tournament map. The shape is the whole of it: one
     * long run between two strongholds, no way round, and a fall that kills
     * anywhere off the line. [ARENA-16]
     *
     * Everything is measured off the original at four centimetres to the
     * unit, which puts three hundred metres between the keeps and makes
     * each of them sixty-odd metres square -- a building our machines can
     * drive into rather than one they would wear.
     */
    /*
     * Measured off the model the player supplied, at 6 cm to the unit: one
     * station every 15 m along the run, each giving where the centre of each
     * lane sits, how wide it is there, and how high. The lanes bulge out to
     * either side a quarter of the way along and pinch back in at the
     * middle, where they all but touch -- that pinch is the one crossing
     * between them, and everywhere else what lies between is the hole.
     * [ARENA-17]
     */
    static const float aafLane[17][5] = {
      /*  A centre  A half   B centre  B half   height */
      { -33.0f, 15.0f,  35.4f, 15.0f,  3.2f },   /* -120 m, off the base */
      { -37.8f, 15.0f,  40.2f, 15.0f,  9.4f },
      { -43.2f, 15.6f,  45.0f, 15.0f, 15.3f },
      { -47.4f, 15.0f,  48.0f, 15.6f, 21.2f },
      { -42.6f, 15.0f,  41.4f, 17.4f, 25.0f },
      { -37.2f, 15.6f,  34.8f, 19.2f, 28.6f },
      { -31.8f, 16.2f,  28.8f, 21.6f, 32.1f },
      { -26.4f, 16.8f,  22.2f, 23.4f, 35.5f },
      { -26.4f, 16.8f,  21.0f, 24.6f, 35.3f },   /* the middle */
      { -30.6f, 16.2f,  28.2f, 22.2f, 31.1f },
      { -34.8f, 15.6f,  36.0f, 20.4f, 26.8f },
      { -39.0f, 15.0f,  43.2f, 18.0f, 22.4f },
      { -43.2f, 14.4f,  49.8f, 15.0f, 18.0f },
      { -41.4f, 13.8f,  46.2f, 13.8f, 13.6f },
      { -38.4f, 14.4f,  40.2f, 13.8f,  9.2f },
      { -34.8f, 14.4f,  33.6f, 14.4f,  4.8f },
      { -31.8f, 15.0f,  36.6f, 24.6f,  2.1f },   /* +120 m, onto the base */
    };
    /*
     * How much wider the causeways are built than they were measured. The
     * original is walked by a man; this is driven by something eight metres
     * across, on ground made of eight-metre cells whose outermost one is a
     * ramp into the void rather than floor. At the measured width there is
     * about a machine and a half of usable lane, which is not a causeway to
     * fight along -- it is a tightrope. The shape, the bow and the pinch are
     * the measurements' own; only the width is ours. [ARENA-19]
     */
    const float fWide = 1.6f;
    const float fStation = 15.0f * m;   /* how far apart those stations are */
    const float fRun = 252.0f * m;      /* base centre to the middle */
    const float fBaseX = 108.0f * m;    /* and half a base, each way */
    const float fBaseZ = 95.0f * m;
    const float fKeep = 45.0f * m;      /* half a keep, wall to wall */
    const float fSkin = 3.0f * m;       /* half the thickness of a wall */
    const float fTall = 34.0f * m;      /* how high the walls stand */
    const float fPier = 20.0f * m;      /* half the pier the doorways flank */
    /* Walls meet at the corners without touching: two quads in one place
     * have nothing to decide which is in front. [MESH-11] */
    const float fJoint = 0.5f * m;
    int iEnd;
    int iStep;

    pArena->byShape = MECHA_ARENA_OPEN;
    pArena->fHalfExtent = 350.0f * m;
    pArena->fWallHeight = 0.0f;
    pArena->iTerrainCells = 80;
    pArena->iFloorTiles = 56;
    pArena->fSkirt = 260.0f * m;
    pArena->fKillY = -30.0f * m;
    pArena->byFloorPalette = MECHA_PAL_FLOOR_B;
    pArena->byGridPalette = MECHA_PAL_GRID;
    pArena->byFloorTile = MECHA_TILE_PLATE_A;
    pArena->byGridTile = MECHA_TILE_PLATE_B;
    pArena->byWallTile = MECHA_TILE_CONCRETE;

    /*
     * Nothing, and then the map painted back onto it: a base at each end and
     * the two lanes between them, laid a station at a time so they follow
     * the curve they were measured on. The lanes climb the whole way in, so
     * leaving a base is uphill and falling back to it is downhill.
     * [ARENA-16]
     */
    mecha_arena_void(pArena, 300.0f * m);
    mecha_arena_pad(pArena, -fRun, 0.0f, fBaseX, fBaseZ, 0.0f);
    mecha_arena_pad(pArena, fRun, 0.0f, fBaseX, fBaseZ, 0.0f);
    for (iStep = 0; iStep < 16; iStep++) {
      const float *pA = aafLane[iStep];
      const float *pB = aafLane[iStep + 1];
      float fX0 = (-8.0f + (float)iStep) * fStation;
      float fX1 = (-7.0f + (float)iStep) * fStation;

      mecha_arena_lane(pArena, fX0, fX1,
                       pA[0] * m, pA[1] * m * fWide, pA[4] * m,
                       pB[0] * m, pB[1] * m * fWide, pB[4] * m);
      mecha_arena_lane(pArena, fX0, fX1,
                       pA[2] * m, pA[3] * m * fWide, pA[4] * m,
                       pB[2] * m, pB[3] * m * fWide, pB[4] * m);
    }

    /*
     * And the last stretch at each end, from where the measurements stop out
     * to the base itself. Without it the causeway ends 24 m short of the
     * ground it is supposed to arrive on.
     */
    {
      const float *pIn = aafLane[0];
      const float *pOut = aafLane[16];
      float fEdge = fRun - fBaseX;

      mecha_arena_lane(pArena, -fEdge, -8.0f * fStation,
                       pIn[0] * m, pIn[1] * m * fWide, 0.0f,
                       pIn[0] * m, pIn[1] * m * fWide, pIn[4] * m);
      mecha_arena_lane(pArena, -fEdge, -8.0f * fStation,
                       pIn[2] * m, pIn[3] * m * fWide, 0.0f,
                       pIn[2] * m, pIn[3] * m * fWide, pIn[4] * m);
      mecha_arena_lane(pArena, 8.0f * fStation, fEdge,
                       pOut[0] * m, pOut[1] * m * fWide, pOut[4] * m,
                       pOut[0] * m, pOut[1] * m * fWide, 0.0f);
      mecha_arena_lane(pArena, 8.0f * fStation, fEdge,
                       pOut[2] * m, pOut[3] * m * fWide, pOut[4] * m,
                       pOut[2] * m, pOut[3] * m * fWide, 0.0f);
    }

    /*
     * And the same two lanes published as ways, station for station, so the
     * computer pilots have something to follow when the ground between them
     * and the enemy is not there. The chain is the measurement table again
     * -- centre and half-width per station -- with an end on each base so a
     * machine in a keep is led out of a doorway rather than at the hole.
     * [AI-13]
     */
    {
      int iLane;

      pArena->iWayCount = 2;
      for (iLane = 0; iLane < 2; iLane++) {
        tMechaWay *pWay = &pArena->aWays[iLane];
        int iCol = iLane * 2;
        float fEdge = fRun - fBaseX;

        pWay->iCount = 0;
        mecha_arena_way_point(pWay, -fRun, aafLane[0][iCol] * m, fBaseZ);
        mecha_arena_way_point(pWay, -fEdge, aafLane[0][iCol] * m,
                              aafLane[0][iCol + 1] * m * fWide);
        for (iStep = 0; iStep <= 16; iStep++)
          mecha_arena_way_point(pWay, (-8.0f + (float)iStep) * fStation,
                                aafLane[iStep][iCol] * m,
                                aafLane[iStep][iCol + 1] * m * fWide);
        mecha_arena_way_point(pWay, fEdge, aafLane[16][iCol] * m,
                              aafLane[16][iCol + 1] * m * fWide);
        mecha_arena_way_point(pWay, fRun, aafLane[16][iCol] * m, fBaseZ);
      }
    }

    /*
     * Where the lanes pinch together the measurements leave six metres
     * between them, which is narrower than the machines that have to use
     * it. The crossing is widened to something two of them can pass on --
     * it is the only way from one lane to the other, and a crossing nobody
     * can take is a hole with extra steps. [ARENA-17]
     */
    mecha_arena_pad(pArena, 0.0f, -7.0f * m, 14.0f * m, 8.0f * m,
                    aafLane[8][4] * m);

    /*
     * The keeps, one to a base. Four walls apiece, with a pier in the middle
     * of the one facing the causeway so there is a doorway either side of it
     * -- and the doorways come out where the lanes are. A single gate on the
     * centreline would open onto the hole between them, which is where the
     * machines went. Open to the sky, because a box here is solid from the
     * ground up and a roof would be a lid with no way under it. [ARENA-16]
     */
    for (iEnd = 0; iEnd < 2; iEnd++) {
      float fCentre = iEnd ? fRun : -fRun;
      float fSign = iEnd ? 1.0f : -1.0f;
      float fBack = fCentre + fSign * (fKeep - fSkin);
      float fFront = fCentre - fSign * (fKeep - fSkin);
      float fFlank = fKeep - 2.0f * fSkin - fJoint;
      int iSide;

      mecha_arena_add_box(pArena, fBack, 0.0f, fSkin, fKeep, fTall,
                          MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
      mecha_arena_face_stone(pArena);
      for (iSide = 0; iSide < 2; iSide++) {
        mecha_arena_add_box(pArena, fCentre,
                            (iSide ? 1.0f : -1.0f) * (fKeep - fSkin),
                            fFlank, fSkin, fTall,
                            MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
        mecha_arena_face_stone(pArena);
      }
      mecha_arena_add_box(pArena, fFront, 0.0f, fSkin, fPier, fTall,
                          MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
      mecha_arena_face_stone(pArena);
    }

    /*
     * No cover out on the run. The original has none either -- what it has
     * is a crest you cannot see over and two lanes that only meet at it --
     * and a block sitting near a lane's edge puts a wall of terrain quads
     * inside its own footprint, which is a mesh nobody can sort. [ARENA-17]
     */

    /* Inside the keeps, which is where this map starts a match. */
    pArena->bySpawnShape = MECHA_SPAWN_BASES;
    pArena->fSpawnHalfX = fRun;
    pArena->fSpawnHalfZ = 30.0f * m;
    break;
  }

  case 4:
    /*
     * A roof: no walls, a sloped hexagonal tabletop in the middle and a
     * block in each corner. The edge runs a long way down. [ARENA-09]
     */
    pArena->byShape = MECHA_ARENA_OPEN;
    pArena->fHalfExtent = 92.0f * m;
    pArena->fWallHeight = 0.0f;
    pArena->byFloorTile = MECHA_TILE_PLATE_A;
    pArena->byGridTile = MECHA_TILE_PLATE_B;
    pArena->byWallTile = MECHA_TILE_RUST;
    pArena->byWallPalette = MECHA_PAL_BLOCK;
    pArena->fKillY = -90.0f * m;
    pArena->fSkirt = 150.0f * m;
    pArena->fMesaTop = 20.0f * m;
    pArena->fMesaBase = 30.0f * m;
    pArena->fMesaHeight = 9.0f * m;
    mecha_arena_add_box(pArena, -61.3f * m, -61.3f * m, 9.0f * m, 9.0f * m,
                        14.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 61.3f * m, -61.3f * m, 9.0f * m, 9.0f * m,
                        14.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -61.3f * m, 61.3f * m, 9.0f * m, 9.0f * m,
                        14.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 61.3f * m, 61.3f * m, 9.0f * m, 9.0f * m,
                        14.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    break;

  default:
    pArena->byFloorTile = MECHA_TILE_PLATE_A;
    pArena->byGridTile = MECHA_TILE_PLATE_B;
    pArena->byWallTile = MECHA_TILE_RUST;
    /* Small and vertical. The centre block is low enough to jump onto and
     * wide enough to fight on, which turns the whole round into a scrap over
     * high ground. */
    pArena->fHalfExtent = 100.0f * m;
    pArena->fWallHeight = 18.0f * m;
    mecha_arena_add_box(pArena, 0.0f, 0.0f, 20.0f * m, 20.0f * m,
                        10.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_HAZARD);
    mecha_arena_add_box(pArena, -68.4f * m, -68.4f * m, 8.0f * m, 8.0f * m,
                        16.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 68.4f * m, -68.4f * m, 8.0f * m, 8.0f * m,
                        16.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -68.4f * m, 68.4f * m, 8.0f * m, 8.0f * m,
                        16.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, 68.4f * m, 68.4f * m, 8.0f * m, 8.0f * m,
                        16.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    break;
  }

  /*
   * Every box now learns what it is standing on, here where the ground is
   * finished. The mesh has always drawn a box up from the terrain under its
   * centre while the collision read the same height as absolute: the two
   * agree on level ground and part company on a slope. One value, read by
   * both. [ARENA-18]
   */
  {
    int iBox;

    for (iBox = 0; iBox < pArena->iObstacleCount; iBox++) {
      tMechaObstacle *pBox = &pArena->aObstacles[iBox];

      pBox->fBaseY = mecha_arena_terrain_height(pArena, pBox->fX, pBox->fZ);
    }
  }

  /*
   * And every way learns where it comes closest to each of the others. Two
   * lanes either side of a hole are one route only at the crossing, and a
   * pilot chasing someone on the far lane has to be told where that is --
   * otherwise it walks its own lane abreast of the enemy for the whole
   * round, which is what it did. Once, here, rather than every tick.
   * [AI-13]
   */
  {
    int iWay;
    int iOther;

    for (iWay = 0; iWay < pArena->iWayCount; iWay++) {
      for (iOther = 0; iOther < MECHA_MAX_WAYS; iOther++) {
        const tMechaWay *pThem;
        float fBest = 0.0f;
        int iBest = 0;
        int i;
        int j;

        pArena->aWays[iWay].aiLink[iOther] = 0;
        if (iOther == iWay || iOther >= pArena->iWayCount)
          continue;
        pThem = &pArena->aWays[iOther];
        for (i = 0; i < pArena->aWays[iWay].iCount; i++) {
          const tMechaWayPoint *pAt = &pArena->aWays[iWay].aPoints[i];

          for (j = 0; j < pThem->iCount; j++) {
            float fGap = mecha_length2(pThem->aPoints[j].fX - pAt->fX,
                                       pThem->aPoints[j].fZ - pAt->fZ);

            if (i == 0 || fGap < fBest) {
              fBest = fGap;
              iBest = i;
            }
          }
        }
        pArena->aWays[iWay].aiLink[iOther] = iBest;
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------

/* See MECHA_OCTAGON_ROOT2 in the header for where this comes from. */
#define MECHA_OCTAGON_DIAGONAL MECHA_OCTAGON_ROOT2

bool mecha_arena_contains(const tMechaArena *pArena, float fX, float fZ)
{
  float fLimit;

  if (!pArena)
    return false;
  fLimit = pArena->fHalfExtent;
  if (fX < -fLimit || fX > fLimit || fZ < -fLimit || fZ > fLimit)
    return false;
  if (pArena->byShape == MECHA_ARENA_OCTAGON
      && fabsf(fX) + fabsf(fZ) > fLimit * MECHA_OCTAGON_DIAGONAL)
    return false;
  return true;
}

//-------------------------------------------------------------------------------------------------

/*
 * Terrain, as a height at every corner of a coarse grid, interpolated inside
 * the cell so a slope is a slope rather than a staircase. Everything else
 * lives in the cell's surface word, as a track chunk's does. [ARENA-10]
 */
static float mecha_arena_cell_size(const tMechaArena *pArena)
{
  return pArena->fHalfExtent * 2.0f / (float)mecha_arena_cells(pArena);
}

/*
 * How far out a point is, measured the way a hexagon measures: the largest
 * of its distances along the three axes that run perpendicular to the three
 * pairs of faces. Inside the apothem on all three is inside the hexagon.
 */
static float mecha_hex_distance(float fX, float fZ)
{
  static const float kafAxis[3][2] = {
    { 1.0f, 0.0f },
    { 0.5f, 0.86602540f },
    { -0.5f, 0.86602540f },
  };
  float fWorst = 0.0f;
  int i;

  for (i = 0; i < 3; i++) {
    float fAlong = fX * kafAxis[i][0] + fZ * kafAxis[i][1];

    if (fAlong < 0.0f)
      fAlong = -fAlong;
    if (fAlong > fWorst)
      fWorst = fAlong;
  }
  return fWorst;
}

//-------------------------------------------------------------------------------------------------

/*
 * The tabletop, answered rather than baked: it is a made thing with six
 * straight edges, and the grid would round them off. The ground mesh gets it
 * for free by sampling this same query. [ARENA-10]
 */
static float mecha_arena_mesa(const tMechaArena *pArena, float fX, float fZ)
{
  float fOut;

  if (pArena->fMesaHeight <= 0.0f || pArena->fMesaBase <= pArena->fMesaTop)
    return 0.0f;
  fOut = mecha_hex_distance(fX, fZ);
  if (fOut <= pArena->fMesaTop)
    return pArena->fMesaHeight;
  if (fOut >= pArena->fMesaBase)
    return 0.0f;
  return pArena->fMesaHeight * (pArena->fMesaBase - fOut)
         / (pArena->fMesaBase - pArena->fMesaTop);
}

//-------------------------------------------------------------------------------------------------

float mecha_arena_mesa_height(const tMechaArena *pArena, float fX, float fZ)
{
  return pArena ? mecha_arena_mesa(pArena, fX, fZ) : 0.0f;
}

//-------------------------------------------------------------------------------------------------
/*
 * Grip, in the race game's own fourteen grades, as a fraction of the best
 * surface. Only the ground's own term is here; the engine bonus is the
 * machine's grip figure and damage is accounted for elsewhere. [ARENA-13]
 */
float mecha_arena_grip_level(int iLevel)
{
  static const float kafGrip[MECHA_GRIP_LEVELS] = {
    1.00f, 0.95f, 0.90f, 0.85f, 0.80f, 0.75f, 0.70f,
    0.65f, 0.60f, 0.55f, 0.50f, 0.40f, 0.30f, 0.20f,
  };

  if (iLevel < 0)
    iLevel = 0;
  if (iLevel >= MECHA_GRIP_LEVELS)
    iLevel = MECHA_GRIP_LEVELS - 1;
  return kafGrip[iLevel];
}

//-------------------------------------------------------------------------------------------------

float mecha_arena_grip(const tMechaArena *pArena, float fX, float fZ)
{
  (void)fX;
  (void)fZ;
  if (!pArena)
    return 1.0f;
  return mecha_arena_grip_level((int)pArena->byGripLevel);
}

//-------------------------------------------------------------------------------------------------

static float mecha_arena_terrain(const tMechaArena *pArena, float fX,
                                 float fZ)
{
  float fCell = mecha_arena_cell_size(pArena);
  float fGridX;
  float fGridZ;
  int iX;
  int iZ;
  float fFracX;
  float fFracZ;
  float fLow;
  float fHigh;

  if (fCell <= 0.0f)
    return 0.0f;
  fGridX = (fX + pArena->fHalfExtent) / fCell;
  fGridZ = (fZ + pArena->fHalfExtent) / fCell;
  if (fGridX < 0.0f)
    fGridX = 0.0f;
  if (fGridZ < 0.0f)
    fGridZ = 0.0f;
  iX = (int)fGridX;
  iZ = (int)fGridZ;
  if (iX > mecha_arena_cells(pArena) - 1)
    iX = mecha_arena_cells(pArena) - 1;
  if (iZ > mecha_arena_cells(pArena) - 1)
    iZ = mecha_arena_cells(pArena) - 1;
  fFracX = mecha_clampf(fGridX - (float)iX, 0.0f, 1.0f);
  fFracZ = mecha_clampf(fGridZ - (float)iZ, 0.0f, 1.0f);

  fLow = pArena->afNode[iZ][iX]
         + (pArena->afNode[iZ][iX + 1] - pArena->afNode[iZ][iX]) * fFracX;
  fHigh = pArena->afNode[iZ + 1][iX]
          + (pArena->afNode[iZ + 1][iX + 1] - pArena->afNode[iZ + 1][iX])
            * fFracX;
  return fLow + (fHigh - fLow) * fFracZ
         + mecha_arena_mesa(pArena, fX, fZ);
}

//-------------------------------------------------------------------------------------------------

float mecha_arena_terrain_height(const tMechaArena *pArena, float fX,
                                 float fZ)
{
  if (!pArena)
    return 0.0f;
  return mecha_arena_terrain(pArena, fX, fZ);
}

//-------------------------------------------------------------------------------------------------

uint32_t mecha_arena_surface(const tMechaArena *pArena, float fX, float fZ)
{
  float fCell;
  int iX;
  int iZ;

  if (!pArena)
    return 0u;
  fCell = mecha_arena_cell_size(pArena);
  if (fCell <= 0.0f)
    return 0u;
  iX = (int)((fX + pArena->fHalfExtent) / fCell);
  iZ = (int)((fZ + pArena->fHalfExtent) / fCell);
  if (iX < 0 || iX >= mecha_arena_cells(pArena) || iZ < 0
      || iZ >= mecha_arena_cells(pArena))
    return 0u;
  return pArena->auiSurface[iZ][iX];
}

//-------------------------------------------------------------------------------------------------

float mecha_arena_ground_height(const tMechaArena *pArena,
                                float fX, float fZ, float fFeetY)
{
  float fBest;
  int i;

  if (!pArena)
    return 0.0f;

  fBest = mecha_arena_terrain(pArena, fX, fZ);

  if (pArena->byShape == MECHA_ARENA_OPEN) {
    /* A platform is a floor from above and nothing at all from below, or a
     * machine drifting back under the roof pops up through it. [ARENA-11] */
    if (!mecha_arena_contains(pArena, fX, fZ))
      return MECHA_ARENA_VOID;
    if (fFeetY < fBest - MECHA_ARENA_STEP_UP)
      return MECHA_ARENA_VOID;
  }

  for (i = 0; i < pArena->iObstacleCount; i++) {
    const tMechaObstacle *pBox = &pArena->aObstacles[i];

    if (fX < pBox->fX - pBox->fHalfX || fX > pBox->fX + pBox->fHalfX)
      continue;
    if (fZ < pBox->fZ - pBox->fHalfZ || fZ > pBox->fZ + pBox->fHalfZ)
      continue;
    /* Below the lip means the box is a wall from here, not a floor. Letting
     * it read as floor is what would teleport a walking mech onto the roof. */
    if (fFeetY < pBox->fBaseY + pBox->fHeight - MECHA_ARENA_STEP_UP)
      continue;
    if (pBox->fBaseY + pBox->fHeight > fBest)
      fBest = pBox->fBaseY + pBox->fHeight;
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

/*
 * Holds a point inside the boundary, whatever shape that is. An open arena
 * has no boundary at all: walking off the edge is the point of it, and what
 * happens next is gravity's business rather than the wall's.
 */
static void mecha_arena_clamp_boundary(const tMechaArena *pArena,
                                       float fLimit, float *pfX, float *pfZ)
{
  if (pArena->byShape == MECHA_ARENA_OPEN)
    return;

  *pfX = mecha_clampf(*pfX, -fLimit, fLimit);
  *pfZ = mecha_clampf(*pfZ, -fLimit, fLimit);

  if (pArena->byShape == MECHA_ARENA_OCTAGON) {
    /* Pushed straight back off the diagonal it crossed, which is the
     * shortest way out of a half-plane. */
    float fDiagonal = fLimit * MECHA_OCTAGON_DIAGONAL;
    float fOver = fabsf(*pfX) + fabsf(*pfZ) - fDiagonal;

    if (fOver > 0.0f) {
      float fHalf = fOver * 0.5f;

      *pfX -= *pfX >= 0.0f ? fHalf : -fHalf;
      *pfZ -= *pfZ >= 0.0f ? fHalf : -fHalf;
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
  mecha_arena_clamp_boundary(pArena, fLimit, pfX, pfZ);

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
  mecha_arena_clamp_boundary(pArena, fLimit, pfX, pfZ);

  return fabsf(*pfX - fStartX) > 1e-3f || fabsf(*pfZ - fStartZ) > 1e-3f;
}

//-------------------------------------------------------------------------------------------------

/*
 * How far a point sits above the ground beneath it, and whether there is any
 * ground there at all. Asking the same terrain query the ground mesh is
 * built from is what makes the shape you see the shape that stops a bullet.
 * [ARENA-12]
 */
static bool mecha_arena_floor_gap(const tMechaArena *pArena, float fX,
                                  float fY, float fZ, float *pfGap)
{
  if (pArena->byShape == MECHA_ARENA_OPEN
      && !mecha_arena_contains(pArena, fX, fZ))
    return false;
  *pfGap = fY - mecha_arena_terrain(pArena, fX, fZ);
  return true;
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

  /* Floor, which is not a plane. */
  {
    float fLength = sqrtf(fDx * fDx + fDy * fDy + fDz * fDz);
    int iSteps = (int)(fLength / MECHA_TRACE_STRIDE) + 1;
    float fPrevT = 0.0f;
    bool bPrevOver;
    float fPrevGap = 0.0f;
    int iStep;

    if (iSteps > MECHA_TRACE_STEPS)
      iSteps = MECHA_TRACE_STEPS;

    bPrevOver = mecha_arena_floor_gap(pArena, fX0, fY0, fZ0, &fPrevGap);
    if (bPrevOver && fPrevGap <= -MECHA_TRACE_SKIN) {
      fBest = 0.0f;                             /* started inside the hill */
    } else {
      for (iStep = 1; iStep <= iSteps; iStep++) {
        float fT = (float)iStep / (float)iSteps;
        float fGap = 0.0f;
        bool bOver = mecha_arena_floor_gap(pArena, fX0 + fDx * fT,
                                           fY0 + fDy * fT,
                                           fZ0 + fDz * fT, &fGap);

        if (bOver && bPrevOver && fPrevGap > -MECHA_TRACE_SKIN
            && fGap <= -MECHA_TRACE_SKIN) {
          float fLo = fPrevT;
          float fHi = fT;
          int iBisect;

          /* The straddling step is short; a dozen halvings put the hit
           * well under a centimetre of the real slope. */
          for (iBisect = 0; iBisect < MECHA_TRACE_BISECT; iBisect++) {
            float fMid = (fLo + fHi) * 0.5f;
            float fMidGap = 0.0f;

            if (mecha_arena_floor_gap(pArena, fX0 + fDx * fMid,
                                      fY0 + fDy * fMid,
                                      fZ0 + fDz * fMid, &fMidGap)
                && fMidGap <= -MECHA_TRACE_SKIN)
              fHi = fMid;
            else
              fLo = fMid;
          }
          if (fHi < fBest)
            fBest = fHi;
          break;
        }
        fPrevT = fT;
        fPrevGap = fGap;
        bPrevOver = bOver;
      }
    }
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

/* Which station of a way is nearest a point, and how far off it that is. */
static int mecha_arena_way_nearest(const tMechaWay *pWay, float fX, float fZ,
                                   float *pfDist)
{
  float fBest = 0.0f;
  int iBest = -1;
  int i;

  for (i = 0; i < pWay->iCount; i++) {
    float fDx = pWay->aPoints[i].fX - fX;
    float fDz = pWay->aPoints[i].fZ - fZ;
    float fDist = mecha_length2(fDx, fDz);

    if (iBest < 0 || fDist < fBest) {
      fBest = fDist;
      iBest = i;
    }
  }
  if (pfDist)
    *pfDist = fBest;
  return iBest;
}

//-------------------------------------------------------------------------------------------------

bool mecha_arena_way_aim(const tMechaArena *pArena, float fX, float fZ,
                         float fToX, float fToZ, float fLook, int iLine,
                         float *pfAimX, float *pfAimZ)
{
  /* Four lines across the way, the way a Whiplash chunk carries four.
   * Sixteen machines walking one line is a queue. [AI-13] */
  static const float afLine[4] = { -0.75f, -0.25f, 0.25f, 0.75f };
  const tMechaWay *pWay = NULL;
  float fBest = 0.0f;
  float fWalked = 0.0f;
  float fAtX = fX;
  float fAtZ = fZ;
  float fDirX = 0.0f;
  float fDirZ = 0.0f;
  float fHalf = 0.0f;
  float fOff;
  int iNear = -1;
  int iGoal = -1;
  int iStep;
  int i;

  if (!pArena || pArena->iWayCount <= 0 || fLook <= 0.0f)
    return false;

  /*
   * The way this machine is on, and the way the enemy is on. Where they are
   * the same, the goal is the station by the enemy; where they differ, it is
   * the crossing between the two -- walking abreast of someone on the far
   * side of a hole gets nobody anywhere. [AI-13]
   */
  {
    const tMechaWay *pTheirs = NULL;
    float fTheirs = 0.0f;
    int iMine = -1;
    int iTheirs = -1;

    for (i = 0; i < pArena->iWayCount; i++) {
      const tMechaWay *pTry = &pArena->aWays[i];
      float fHere = 0.0f;
      float fThere = 0.0f;
      int iHere = mecha_arena_way_nearest(pTry, fX, fZ, &fHere);
      int iThere = mecha_arena_way_nearest(pTry, fToX, fToZ, &fThere);

      if (iHere < 0 || iThere < 0)
        continue;
      if (!pWay || fHere < fBest) {
        fBest = fHere;
        pWay = pTry;
        iNear = iHere;
        iMine = i;
      }
      if (!pTheirs || fThere < fTheirs) {
        fTheirs = fThere;
        pTheirs = pTry;
        iTheirs = i;
      }
    }
    if (!pWay || iMine < 0 || iTheirs < 0)
      return false;
    iGoal = iMine == iTheirs
              ? mecha_arena_way_nearest(pWay, fToX, fToZ, NULL)
              : pWay->aiLink[iTheirs];
  }
  if (iNear == iGoal)
    return false;

  iStep = iGoal > iNear ? 1 : -1;

  /*
   * Then the aim point, which is Whiplash's: walk the chain from here by the
   * look-ahead and interpolate where that lands, rather than pointing at a
   * station and turning on the spot when it arrives.
   */
  for (i = iNear; i != iGoal; i += iStep) {
    const tMechaWayPoint *pNext = &pWay->aPoints[i + iStep];
    float fDx = pNext->fX - fAtX;
    float fDz = pNext->fZ - fAtZ;
    float fLeg = mecha_length2(fDx, fDz);

    if (fLeg < 0.01f)
      continue;
    fDirX = fDx / fLeg;
    fDirZ = fDz / fLeg;
    fHalf = pNext->fHalf;
    if (fWalked + fLeg >= fLook) {
      float fRest = fLook - fWalked;

      fAtX += fDirX * fRest;
      fAtZ += fDirZ * fRest;
      fWalked = fLook;
      break;
    }
    fWalked += fLeg;
    fAtX = pNext->fX;
    fAtZ = pNext->fZ;
  }
  if (fWalked <= 0.0f)
    return false;

  /*
   * And the line across it. The station says how wide the way was measured,
   * but the ground is a grid of cells with the heights interpolated between
   * them, so the outermost cell of any edge is a ramp into the void rather
   * than floor: a machine standing on it is already on its way down. The
   * line is laid out on what is left after that. [AI-13]
   */
  if (iLine < 0 || iLine > 3)
    iLine = 0;
  fHalf -= pArena->fHalfExtent * 2.0f / (float)mecha_arena_cells(pArena);
  if (fHalf < 0.0f)
    fHalf = 0.0f;
  fOff = afLine[iLine] * fHalf;
  if (pfAimX)
    *pfAimX = fAtX + fDirZ * fOff;
  if (pfAimZ)
    *pfAimZ = fAtZ - fDirX * fOff;
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

  /*
   * A stronghold at each end: the machines start inside them, alternating
   * ends and spread across the width of a base, so a duel opens with one in
   * each keep and a free-for-all fills both courtyards. Which is also how
   * the map it is taken from starts a match. [ARENA-14]
   */
  if (pArena->bySpawnShape == MECHA_SPAWN_BASES) {
    int iPerEnd = (iCount + 1) / 2;
    int iPair = (iSlot % iCount) / 2;
    /* One a side stands off the middle rather than on it, or it starts
     * facing the pier between the two doorways instead of a way out. */
    float fSpread = iPerEnd > 1
                      ? 2.0f * (float)iPair / (float)(iPerEnd - 1) - 1.0f
                      : ((iSlot & 1) ? 1.0f : -1.0f);
    float fX = (iSlot & 1) ? pArena->fSpawnHalfX : -pArena->fSpawnHalfX;
    float fZ = fSpread * pArena->fSpawnHalfZ;

    if (pfX) *pfX = fX;
    if (pfZ) *pfZ = fZ;
    /* Facing out of the gate, down the causeway. */
    if (piFacing)
      *piFacing = fX > 0.0f ? MECHA_ANGLE_QUARTER * 3 : MECHA_ANGLE_QUARTER;
    return;
  }

  if (pfX) *pfX = mecha_sin(iAngle) * fRing;
  if (pfZ) *pfZ = mecha_cos(iAngle) * fRing;
  /* Facing the middle is the opposite heading. */
  if (piFacing) *piFacing = mecha_angle_wrap(iAngle + MECHA_ANGLE_HALF);
}
