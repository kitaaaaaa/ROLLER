#include "mecha_mode.h"

#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_input.h"
#include "mecha_mesh.h"
#include "mecha_render.h"
#include "mecha_sim.h"

#include "3d.h"
#include "frontend.h"
#include "func2.h"
#include "game_render.h"

#include <SDL3/SDL.h>
#include <string.h>


//-------------------------------------------------------------------------------------------------
/*
 * The simulation runs at a fixed 60 Hz whatever the display is doing, so a
 * match plays identically on any machine and stays reproducible from its
 * seed. A frame that took too long is allowed to catch up over a few ticks
 * and no further; without that cap, one long stall (a window drag, a
 * breakpoint) would be paid back as a burst of simulation the player cannot
 * react to.
 */
#define MECHA_TICK_NS (1000000000ull / (uint64)MECHA_TICK_HZ)
#define MECHA_MAX_CATCHUP_TICKS 5

//-------------------------------------------------------------------------------------------------

static tMechaWorld s_World;
static tMechaCamera s_Camera;
static tMechaQuad s_aQuads[MECHA_QUAD_CAPACITY];

static int s_iPlayerDef = 0;
static int s_iOpponentDef = 2;
static int s_iArenaIdx = 0;
static int s_iRoundsToWin = 2;

static int s_iPlayerIdx = -1;
static uint64 s_ullLastTimeNs;
static uint64 s_ullAccumulatorNs;
static bool s_bQuitHeld;
static bool s_bActive;

/* Restored on exit so the frontend and the race find the screen globals the
 * way they left them. */
static GameRenderMode s_ePreviousRenderMode;
static int s_iSavedWinX;
static int s_iSavedWinY;
static int s_iSavedWinW;
static int s_iSavedWinH;
static int s_iSavedScrSize;
static int s_iSavedXBase;
static int s_iSavedYBase;
static uint8 *s_pSavedScreenPointer;

//-------------------------------------------------------------------------------------------------

void mecha_mode_configure(int iPlayerDef, int iOpponentDef, int iArenaIdx,
                          int iRoundsToWin)
{
  int iDefCount = mecha_def_count();
  int iArenaCount = mecha_arena_count();

  s_iPlayerDef = iPlayerDef < 0 ? 0 : iPlayerDef % iDefCount;
  s_iOpponentDef = iOpponentDef < 0 ? 0 : iOpponentDef % iDefCount;
  s_iArenaIdx = iArenaIdx < 0 ? 0 : iArenaIdx % iArenaCount;
  s_iRoundsToWin = iRoundsToWin > 0 ? iRoundsToWin : 1;
}

//-------------------------------------------------------------------------------------------------

const char *mecha_mode_mech_name(int iDefIdx)
{
  return mecha_def_get(iDefIdx)->szName;
}

//-------------------------------------------------------------------------------------------------

const char *mecha_mode_arena_name(int iArenaIdx)
{
  return mecha_arena_name(iArenaIdx);
}

//-------------------------------------------------------------------------------------------------

int mecha_mode_mech_count(void)
{
  return mecha_def_count();
}

//-------------------------------------------------------------------------------------------------

int mecha_mode_arena_count(void)
{
  return mecha_arena_count();
}

//-------------------------------------------------------------------------------------------------

void mecha_mode_enter(void)
{
  s_iSavedWinX = winx;
  s_iSavedWinY = winy;
  s_iSavedWinW = winw;
  s_iSavedWinH = winh;
  s_iSavedScrSize = scr_size;
  s_iSavedXBase = xbase;
  s_iSavedYBase = ybase;
  s_pSavedScreenPointer = screen_pointer;

  /* The mode is built around the software rasteriser: it sorts its own
   * geometry back to front because that path has no depth buffer, and it
   * writes the HUD straight into the indexed frame. Forcing the mode here
   * means it behaves the same whatever the player left the renderer set to. */
  s_ePreviousRenderMode = game_render_get_mode(g_pGameRenderer);
  game_render_set_mode(g_pGameRenderer, GAME_RENDER_SOFTWARE);

  mecha_sim_init(&s_World, s_iArenaIdx, (uint32)SDL_GetTicks() | 1u,
                 s_iRoundsToWin);
  s_iPlayerIdx = mecha_sim_add_mech(&s_World, s_iPlayerDef,
                                    MECHA_CONTROL_HUMAN, 0);
  mecha_sim_add_mech(&s_World, s_iOpponentDef, MECHA_CONTROL_AI, 1);
  mecha_sim_begin_match(&s_World);

  mecha_camera_reset(&s_Camera);
  if (s_iPlayerIdx >= 0)
    mecha_camera_update(&s_Camera, &s_World, s_iPlayerIdx);

  s_ullLastTimeNs = SDL_GetTicksNS();
  s_ullAccumulatorNs = 0;
  /* Whatever key sent us here may still be down. */
  s_bQuitHeld = true;
  s_bActive = true;
}

//-------------------------------------------------------------------------------------------------

void mecha_mode_update(void)
{
  tMechaInput input;
  tMechaInput aInputs[MECHA_MAX_MECHS];
  uint64 ullNow;
  uint64 ullElapsed;
  bool bQuit;
  int iTicks = 0;

  if (!s_bActive)
    return;

  bQuit = mecha_input_quit_pressed();
  if (bQuit && !s_bQuitHeld) {
    frontend_set_state(eFRONTEND_STATE_MAIN_MENU);
    s_bQuitHeld = true;
    return;
  }
  s_bQuitHeld = bQuit;

  ullNow = SDL_GetTicksNS();
  ullElapsed = ullNow >= s_ullLastTimeNs ? ullNow - s_ullLastTimeNs : 0;
  s_ullLastTimeNs = ullNow;
  s_ullAccumulatorNs += ullElapsed;

  /* One poll per frame, reused across the catch-up ticks: sampling the pad
   * again mid-catch-up would read the same hardware state anyway. */
  mecha_input_poll(&input);
  memset(aInputs, 0, sizeof(aInputs));
  if (s_iPlayerIdx >= 0 && s_iPlayerIdx < MECHA_MAX_MECHS)
    aInputs[s_iPlayerIdx] = input;

  while (s_ullAccumulatorNs >= MECHA_TICK_NS
         && iTicks < MECHA_MAX_CATCHUP_TICKS) {
    mecha_sim_tick(&s_World, aInputs, MECHA_MAX_MECHS);
    s_ullAccumulatorNs -= MECHA_TICK_NS;
    iTicks++;
  }
  if (s_ullAccumulatorNs >= MECHA_TICK_NS * MECHA_MAX_CATCHUP_TICKS) {
    /* Too far behind to make up. Drop the debt rather than spiral. */
    s_ullAccumulatorNs = 0;
  }

  if (s_iPlayerIdx >= 0)
    mecha_camera_update(&s_Camera, &s_World, s_iPlayerIdx);
}

//-------------------------------------------------------------------------------------------------

void mecha_mode_draw(void)
{
  if (!s_bActive || s_iPlayerIdx < 0 || !scrbuf)
    return;

  game_render_begin_frame(g_pGameRenderer);
  mecha_render_frame(g_pGameRenderer, &s_World, &s_Camera, s_iPlayerIdx,
                     scrbuf, XMAX, YMAX, s_aQuads, MECHA_QUAD_CAPACITY);
  game_render_end_frame(g_pGameRenderer);
}

//-------------------------------------------------------------------------------------------------

void mecha_mode_exit(void)
{
  if (!s_bActive)
    return;

  game_render_set_mode(g_pGameRenderer, s_ePreviousRenderMode);

  winx = s_iSavedWinX;
  winy = s_iSavedWinY;
  winw = s_iSavedWinW;
  winh = s_iSavedWinH;
  scr_size = s_iSavedScrSize;
  xbase = s_iSavedXBase;
  ybase = s_iSavedYBase;
  screen_pointer = s_pSavedScreenPointer;

  s_bActive = false;
  s_iPlayerIdx = -1;
}
