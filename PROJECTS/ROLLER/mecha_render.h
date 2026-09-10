#ifndef _ROLLER_MECHA_RENDER_H
#define _ROLLER_MECHA_RENDER_H
//-------------------------------------------------------------------------------------------------
/*
 * The engine-facing half of the arena mode: camera, quad submission and HUD.
 *
 * Everything is drawn through ROLLER's software rasteriser, the same path
 * the track game uses -- game_render_set_camera / set_projection to load the
 * view, then game_render_quad_world per polygon. The mode forces software
 * mode on entry because it depends on that path's behaviour (in particular
 * on there being no depth buffer, which is why it sorts).
 *
 * This is the only file in the mode that includes SDL or touches a ROLLER
 * global; everything behind it is testable on its own.
 */
//-------------------------------------------------------------------------------------------------
#include "game_render.h"
#include "mecha_mesh.h"
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

/*
 * Third-person chase camera. It keeps its own smoothed position between
 * frames -- snapping it straight onto the lock every frame reads as a
 * camera fighting the player.
 */
typedef struct
{
  float fX, fY, fZ;
  int   iYaw;               /* 14-bit, same circle as every other heading */
  int   iPitch;
  bool  bSettled;           /* false until the first update places it */
} tMechaCamera;

void mecha_camera_reset(tMechaCamera *pCamera);

/* Frames iViewMech and whatever it has locked. Call once per rendered frame,
 * before mecha_render_frame. */
void mecha_camera_update(tMechaCamera *pCamera, const tMechaWorld *pWorld,
                         int iViewMech);

//-------------------------------------------------------------------------------------------------

/*
 * Draws one frame of the arena into pScrBuf, an 8-bit indexed buffer
 * iWidth x iHeight with a row stride of iWidth -- the same buffer layout
 * draw_road renders the race into.
 *
 * The caller owns the frame lifecycle (game_render_begin_frame /
 * end_frame) and the quad scratch buffer, so nothing here allocates.
 */
void mecha_render_frame(GameRenderer *pRenderer, const tMechaWorld *pWorld,
                        const tMechaCamera *pCamera, int iViewMech,
                        uint8 *pScrBuf, int iWidth, int iHeight,
                        tMechaQuad *paScratch, int iScratchCapacity);

//-------------------------------------------------------------------------------------------------
/* Exposed for the mode's own overlays and for tests. */

/* Projects a world point to buffer pixels. Returns false when the point is
 * behind the camera, in which case the outputs are untouched. */
bool mecha_render_project(const tMechaCamera *pCamera, int iWidth, int iHeight,
                          float fX, float fY, float fZ,
                          int *piScreenX, int *piScreenY);

/*
 * Fills a 256-entry palette with the colours the arena mode paints in.
 *
 * The mode generates all of its own geometry and never loads the retail
 * data, so it cannot rely on the game's palette being present either --
 * pal_addr is only populated by the states that load assets, and presenting
 * an indexed frame through an empty palette turns every pixel black. This is
 * the authoritative table; mecha_mode.c installs it on entry and the
 * headless render test asserts against it.
 *
 * Values are the game's 6-bit levels (0..63), matching tColor everywhere
 * else in the engine.
 */
void mecha_render_build_palette(tColor *paPalette);

/*
 * True when the mode is drawing effects from the game's own texture bank
 * rather than as flat-shaded particles.
 *
 * Exposed for tests, which otherwise measure the wrong thing: a blast drawn
 * from gentex.drh paints none of the palette index the flat path uses, so a
 * test that counts that index reads zero and concludes nothing rendered. It
 * only becomes true once a frame has actually been submitted, since the bank
 * is loaded lazily on the first effect that wants it.
 */
/* Loads whatever retail assets the mode can use -- currently the HUD font.
 * Safe to call with none present; the mode falls back to its own font. */
void mecha_render_init_assets(GameRenderer *pRenderer);

/* True when HUD text is being drawn in the game's own font rather than the
 * built-in one. Those glyphs carry retail palette indices, so a test that
 * checks every colour on screen belongs to this mode has to know. */
bool mecha_render_font_is_retail(void);

bool mecha_render_sprites_active(void);

/* How many recoloured copies of the effect bank came up. Three when the
 * game's data and palette are both there, zero without them -- in which
 * case every machine's fire is the same blue and the mode still runs. */
int mecha_render_tints_active(void);

/* True when index byIndex has a colour of its own in the table above rather
 * than the neutral fill. Lets the render test catch a quad that paints with
 * an index nobody gave a colour to. */
bool mecha_render_palette_defines(int iIndex);

/* Five-by-seven text, scaled by iScale. Returns the x coordinate just past
 * the string. Unknown characters render as blanks. */
int mecha_render_text(uint8 *pScrBuf, int iWidth, int iHeight,
                      int iX, int iY, int iScale, uint8 byColour,
                      const char *szText);
int mecha_render_text_width(int iScale, const char *szText);

/*
 * The same, in the game's larger sprite face.
 *
 * There are two retail fonts and they are not interchangeable: minitext.bm
 * is the restricted set the race HUD labels things with, and font6.bm is
 * what the game announces things in. Titles and round banners want the
 * second. With no retail data both fall back to the built-in font, the
 * large one simply drawn at twice the scale.
 */
int mecha_render_text_large(uint8 *pScrBuf, int iWidth, int iHeight,
                            int iX, int iY, int iScale, uint8 byColour,
                            const char *szText);
int mecha_render_text_large_width(int iScale, const char *szText);

void mecha_render_fill(uint8 *pScrBuf, int iWidth, int iHeight,
                       int iX, int iY, int iW, int iH, uint8 byColour);

//-------------------------------------------------------------------------------------------------
/*
 * The briefing screen.
 *
 * Shown before the first match and returned to after every match, so there
 * is somewhere to stand that is neither a fight nor the exit. It draws into the same
 * indexed buffer as everything else and needs no renderer, no camera and no
 * world, which is what lets the headless test cover it.
 *
 * The caller owns the rows: it supplies the labels, the values it wants
 * cycled, and which row is selected. Rows with a NULL szValue are actions
 * rather than settings.
 */
#define MECHA_BRIEF_MAX_ROWS 12

typedef struct
{
  const char *szLabel;
  const char *szValue;      /* NULL for an action row */
} tMechaBriefRow;

typedef struct
{
  const char     *szResult;   /* how the last match ended; NULL on arrival */
  bool            bResultWin;
  int             iSelection; /* index into aRows */
  int             iRowCount;
  tMechaBriefRow  aRows[MECHA_BRIEF_MAX_ROWS];
} tMechaBriefing;

void mecha_render_briefing(const tMechaBriefing *pBrief, uint8 *pScrBuf,
                           int iWidth, int iHeight);

/* The controls, on a page of their own, reached from the briefing. Takes no
 * state: there is nothing on it to choose. */
void mecha_render_controls(uint8 *pScrBuf, int iWidth, int iHeight);

//-------------------------------------------------------------------------------------------------
#endif
