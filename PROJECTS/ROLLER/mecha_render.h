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

/* Five-by-seven text, scaled by iScale. Returns the x coordinate just past
 * the string. Unknown characters render as blanks. */
int mecha_render_text(uint8 *pScrBuf, int iWidth, int iHeight,
                      int iX, int iY, int iScale, uint8 byColour,
                      const char *szText);
int mecha_render_text_width(int iScale, const char *szText);

void mecha_render_fill(uint8 *pScrBuf, int iWidth, int iHeight,
                       int iX, int iY, int iW, int iH, uint8 byColour);

//-------------------------------------------------------------------------------------------------
#endif
