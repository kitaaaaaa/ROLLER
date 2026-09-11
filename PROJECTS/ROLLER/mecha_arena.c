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
/*
 * The green arena. 244-255 is a black-to-green ramp in the game's own
 * palette and 48-63 a brown one, so the grass, the canopy and the bark come
 * off those rather than being borrowed from the tracer colours; stone stays
 * one of the greys. A field checkered green against grey read as a chess
 * board, which is the one thing a meadow must not look like.
 */
#define MECHA_PAL_GRASS_A   249
#define MECHA_PAL_GRASS_B   246
#define MECHA_PAL_BARK      57
#define MECHA_PAL_LEAF      252
#define MECHA_PAL_ROCK      125
#define MECHA_PAL_ROCK_TOP  130

//-------------------------------------------------------------------------------------------------
/*
 * Marching a shot across the ground.
 *
 * The stride is what decides whether a bullet can step over a hillside
 * between two samples. A metre against hills forty metres wide leaves no
 * gap to step through, and the fastest shot in the game covers seven
 * metres in a tick, so a segment is eight samples at worst. The cap is
 * only there so an absurdly long query cannot turn into an unbounded loop.
 */
#define MECHA_TRACE_STRIDE  MECHA_M(1.0f)
#define MECHA_TRACE_STEPS   48
#define MECHA_TRACE_BISECT  12
/*
 * How far under the surface counts as still being above it.
 *
 * The gun car's weapon floats six metres off its right flank, so parked
 * across the steepest hillside in Coldwater Meadow its muzzle dips about
 * three centimetres into the slope. Sweeping every machine over every
 * square metre of that arena at sixteen facings found that in 24 of 2.28
 * million samples -- rare, and unplayable where it happens, because a shot
 * that begins underground detonates at the muzzle.
 *
 * So the ground is treated as beginning a little below where it is drawn.
 * Twenty centimetres is six times the worst graze measured and small
 * enough to be invisible: a shot stopping into a slope stops a fifth of a
 * metre late along the normal, on hills that stand twenty-six metres.
 */
#define MECHA_TRACE_SKIN    MECHA_M(0.2f)

//-------------------------------------------------------------------------------------------------

#define MECHA_ARENA_COUNT 5

/* Nothing may be pushed further than this in one resolve pass. A mech that
 * somehow ends up deep inside geometry crawls out over a few ticks instead of
 * being flung across the arena. */
#define MECHA_ARENA_MAX_PUSH (6.0f * MECHA_METRE)

//-------------------------------------------------------------------------------------------------

/*
 * Terrain helpers.
 *
 * A hill is raised by hand rather than by noise: pick a middle, a reach and
 * a height, and every grid corner inside it comes up by a cosine of its
 * distance. Angular, because the corners are all the ground has -- a hill
 * built this way is a dozen facets, which is what it should look like.
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
       * A truncated cone: flat on top, straight down the sides, and a
       * definite edge where the two meet.
       *
       * Every part of that is doing something. The straight sides are one
       * constant grade, which is what a ramp is -- a smooth shoulder
       * launches nothing, because by the time the machine is fast the
       * slope has flattened out under it. The flat top is somewhere to
       * land and fight, and it also stops a walker hopping the apex: a
       * cone that comes to a point drops out from under anything that
       * crosses it, boost or no boost. And the edge between them is the
       * lip the launch comes off.
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

  case 3: {
    /*
     * Open country: eight sides, hills you can be thrown off, and nothing
     * built on it. The ground is not magnetic, which is the whole point --
     * boost up one of these and you leave it at the top.
     *
     * And no walls you can see. The boundary is still there and still stops
     * a machine, but what is drawn past it is more forest: ground running
     * out to twice the arena again with trees standing on it, so the edge
     * of the fight is a place the fight stops rather than a place the world
     * does. A wall in a meadow is a fence around a field.
     */
    int iTree;

    pArena->byShape = MECHA_ARENA_OCTAGON;
    pArena->fHalfExtent = 260.0f * m;
    pArena->fWallHeight = 0.0f;
    pArena->iFloorTiles = 40;
    /* Twice the arena, twice the grid: the hills are the same size they
     * always were, and a grid stretched to cover twice the ground would
     * have rounded them off into bumps. */
    pArena->iTerrainCells = 24;
    pArena->fOuterReach = 560.0f * m;
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

  case 4:
    /*
     * A roof. No walls at all -- walk off it and you are falling -- with a
     * raised hexagonal tabletop in the middle of it and a block in each
     * corner to fight around. The tabletop is sloped rather than sheer, so
     * it is high ground you take rather than a wall you go round, and its
     * edges stay hexagonal because it is answered by the height query
     * instead of being pressed into the terrain grid.
     *
     * The edge runs a long way down. It is the top of a tower, and a tower
     * that stops six metres below its own roof is a table.
     */
    pArena->byShape = MECHA_ARENA_OPEN;
    pArena->fHalfExtent = 78.0f * m;
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
    mecha_arena_add_box(pArena, -52.0f * m, -52.0f * m, 9.0f * m, 9.0f * m,
                        14.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,  52.0f * m, -52.0f * m, 9.0f * m, 9.0f * m,
                        14.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena, -52.0f * m,  52.0f * m, 9.0f * m, 9.0f * m,
                        14.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
    mecha_arena_add_box(pArena,  52.0f * m,  52.0f * m, 9.0f * m, 9.0f * m,
                        14.0f * m, MECHA_PAL_BLOCK, MECHA_PAL_BLOCK_TOP);
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
 * Terrain, as a height at every corner of a coarse grid.
 *
 * The cell a point falls in is found by index and the height inside it
 * interpolated between its four corners, which is what makes a slope a
 * slope rather than a staircase. Everything else about the ground -- the
 * pit, whether a machine is stuck to it -- lives in the cell's surface
 * word, exactly as a track chunk's does.
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
 * The tabletop, answered rather than baked.
 *
 * The hills go into the grid because they are meant to be lumpy -- a dozen
 * facets is what a hill built out of corners should look like. A tabletop
 * is not: it is a made thing with six straight edges, and rounding those
 * off to the nearest grid corner would lose the only thing that says
 * somebody built it. So it is computed here instead, and the ground mesh
 * picks it up for free because the mesh samples this same query at every
 * corner it draws.
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
    /*
     * A platform is a floor from above and nothing at all from below. Off
     * the edge there is no floor to be at any height, and underneath it
     * there is none either -- without that second half, a machine that has
     * fallen past the edge and drifted back beneath the roof pops up
     * through it, which is the same mistake as walking into the side of a
     * box and being teleported onto its roof.
     */
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
 * How far a point sits above the ground beneath it, and whether there is
 * any ground beneath it at all.
 *
 * The floor used to be the y = 0 plane, which was true of the first three
 * arenas and of nothing since. A shot crossing Coldwater Meadow passed
 * clean through every hill it met, and one fired across Tower Seven went
 * through the tabletop, because neither is at zero. The terrain query is
 * what the ground mesh is built from, so asking it here is what makes the
 * shape you can see the shape that stops a bullet.
 *
 * A platform arena has ground only where the platform is; past the edge a
 * shot keeps going rather than striking a floor that is not there.
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
