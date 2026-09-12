#include "mecha_mode.h"

#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_input.h"
#include "mecha_mesh.h"
#include "mecha_render.h"
#include "mecha_sim.h"
#include "mecha_sound.h"
#include "view.h"
#include "roller.h"

#include "3d.h"
#include "frontend.h"
#include "func2.h"
#include "game_render.h"
#include "graphics.h"
#include "roller.h"
#include "sound.h"

#include <fcntl.h>
#ifdef IS_WINDOWS
#include <io.h>
#define close _close
#else
#include <unistd.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <SDL3/SDL.h>
#include <string.h>


//-------------------------------------------------------------------------------------------------
/*
 * A fixed 60 Hz whatever the display does, with a cap on how much a slow
 * frame may catch up. [MODE-03]
 */
#define MECHA_TICK_NS (1000000000ull / (uint64)MECHA_TICK_HZ)
#define MECHA_MAX_CATCHUP_TICKS 5

/* How long VICTORY or DEFEAT is left on screen before the briefing comes
 * back, and how a held direction repeats on the briefing itself. */
#define MECHA_RESULT_HOLD_NS   2500000000ull
#define MECHA_MENU_DELAY_NS     350000000ull
#define MECHA_MENU_REPEAT_NS    110000000ull

//-------------------------------------------------------------------------------------------------
/* Three screens: the briefing sets a match up and is the only way out, the
 * controls are a page off it, and the match is the fight. */
typedef enum
{
  MECHA_SCREEN_BRIEFING = 0,
  MECHA_SCREEN_MATCH    = 1,
  MECHA_SCREEN_CONTROLS = 2
} eMechaScreen;

/* Briefing rows, in the order they are drawn. */
typedef enum
{
  MECHA_ROW_START = 0,
  MECHA_ROW_MODE,
  MECHA_ROW_MECH,
  MECHA_ROW_COLOURS,
  MECHA_ROW_OPPONENT,
  MECHA_ROW_ARENA,
  MECHA_ROW_SKILL,
  MECHA_ROW_TIME,
  MECHA_ROW_HOLD_FIRE,
  MECHA_ROW_SPECTATE,
  MECHA_ROW_CONTROLS,
  MECHA_ROW_EXIT,
  MECHA_ROW_COUNT
} eMechaBriefRowId;

/*
 * What the round clock can be set to. Zero is a deathmatch: no clock, and
 * so no round ever decided on who has the most armour left when it runs
 * out, which is a different game rather than a longer one.
 */
static const int s_aiRoundSeconds[] = { 30, 60, 90, 120, 0 };

/*
 * A duel is two machines. Survival fills the arena: one of everything the
 * roster has, over and over, until the world is full -- sixteen of them on
 * MERIDIAN CROSSING costs a fifth of a second of simulation for a minute of
 * fighting, and needs about five thousand quads at its worst. [MODE-04]
 */
typedef enum
{
  MECHA_GAME_DUEL = 0,
  MECHA_GAME_SURVIVAL,
  MECHA_GAME_COUNT
} eMechaGameMode;

static const char *mecha_mode_game_name(int iMode)
{
  switch (iMode) {
  case MECHA_GAME_SURVIVAL: return "SURVIVAL";
  default:                  return "DUEL";
  }
}

#define MECHA_ROUND_CHOICES \
  ((int)(sizeof(s_aiRoundSeconds) / sizeof(s_aiRoundSeconds[0])))

//-------------------------------------------------------------------------------------------------

static tMechaWorld s_World;
static tMechaCamera s_Camera;
static tMechaQuad s_aQuads[MECHA_QUAD_CAPACITY];

static int s_iPlayerDef = 0;
static int s_iOpponentDef = 2;
static int s_iArenaIdx = 0;
static int s_iRoundsToWin = 2;
static int s_iAiSkill = MECHA_AI_VETERAN;
/* Index into s_aiRoundSeconds; starts on the default the simulation uses. */
static int s_iRoundChoice = 2;
static bool s_bAiHoldFire;
static int s_iGameMode = MECHA_GAME_DUEL;
static int s_iScheme;
/* No machine of the player's own: the camera flies and the fight runs
 * without them. [MODE-05] */
static bool s_bSpectate;

static int s_iPlayerIdx = -1;
static uint64 s_ullLastTimeNs;
static uint64 s_ullAccumulatorNs;
static bool s_bQuitHeld;
static bool s_bActive;

static eMechaScreen s_eScreen;
static int s_iBriefSelection;
static const char *s_szLastResult;
/* Where a spectated match's result line is built; s_szLastResult only
 * borrows it. */
static char s_szSpectateResult[40];
static bool s_bLastResultWin;
/* Counts down from MECHA_RESULT_HOLD_NS once the match is decided. */
static uint64 s_ullResultHoldNs;

/* Held-to-repeat state for the briefing's four directions, plus plain edge
 * detection for the two buttons. */
typedef struct
{
  bool   bHeld;
  uint64 ullNextNs;
} tMechaRepeat;

static tMechaRepeat s_Up;
static tMechaRepeat s_Down;
static tMechaRepeat s_Left;
static tMechaRepeat s_Right;
static bool s_bConfirmHeld;
static bool s_bBackHeld;

/* Restored on exit so the frontend and the race find the screen globals the
 * way they left them. */
static GameRenderMode s_ePreviousRenderMode;
/* Set when this mode had to create the renderer itself, so exit knows
 * whether to tear it down or just hand the old mode back. */
static bool s_bCreatedRenderer;

/*
 * The mode's own palette. Presentation reads pal_addr, which only the states
 * that load retail data fill in, so coming in on --arena without this
 * rasterises correctly and presents black. Put back on exit. [MODE-01]
 */
static tColor s_aArenaPalette[256];
static tColor s_aSavedPalette[256];
static tColor *s_pSavedPalAddr;
/* Whose memory pal_addr points at, in the engine's own terms: negative
 * means nobody's, and setpal leaves it alone. */
static void *s_pSavedPalSelector;
static bool s_bPaletteInstalled;

/* A palette counts as loaded once any entry is non-black; an all-zero table
 * presents every frame as black whatever the renderer drew. */
static bool mecha_mode_palette_loaded(void)
{
  int i;

  if (!pal_addr)
    return false;
  for (i = 0; i < 256; i++) {
    if (pal_addr[i].byR || pal_addr[i].byG || pal_addr[i].byB)
      return true;
  }
  return false;
}
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

void mecha_mode_set_ai_skill(int iSkill)
{
  if (iSkill < 0)
    iSkill = 0;
  if (iSkill >= MECHA_AI_SKILL_COUNT)
    iSkill = MECHA_AI_SKILL_COUNT - 1;
  s_iAiSkill = iSkill;
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

const char *mecha_mode_skill_name(int iSkill)
{
  return mecha_sim_ai_skill_name(iSkill);
}

//-------------------------------------------------------------------------------------------------

int mecha_mode_skill_count(void)
{
  return MECHA_AI_SKILL_COUNT;
}

//-------------------------------------------------------------------------------------------------

/*
 * Whether there is a game to leave for. The arena runs on its own; the menus
 * it would hand back to do not. Probed once -- a missing install will not
 * appear mid-match.
 */
static bool mecha_file_present(const char *szFile)
{
  int iFile;

  if (!szFile || !szFile[0])
    return false;
  iFile = ROLLERopen(szFile, O_RDONLY | O_BINARY);
  if (iFile == -1)
    return false;
  close(iFile);
  return true;
}

static bool mecha_mode_retail_present(void)
{
  static bool s_bChecked;
  static bool s_bPresent;

  if (!s_bChecked) {
    s_bChecked = true;
    s_bPresent = mecha_file_present(gencartex_name);
  }
  return s_bPresent;
}

//-------------------------------------------------------------------------------------------------

/* True on the frame a direction goes down, and again every repeat interval
 * while it stays down. */
static bool mecha_mode_repeat(tMechaRepeat *pRepeat, bool bDown,
                              uint64 ullNowNs)
{
  if (!bDown) {
    pRepeat->bHeld = false;
    return false;
  }
  if (!pRepeat->bHeld) {
    pRepeat->bHeld = true;
    pRepeat->ullNextNs = ullNowNs + MECHA_MENU_DELAY_NS;
    return true;
  }
  if (ullNowNs >= pRepeat->ullNextNs) {
    pRepeat->ullNextNs = ullNowNs + MECHA_MENU_REPEAT_NS;
    return true;
  }
  return false;
}

//-------------------------------------------------------------------------------------------------

static int mecha_mode_wrap(int iValue, int iCount)
{
  if (iCount <= 0)
    return 0;
  iValue %= iCount;
  return iValue < 0 ? iValue + iCount : iValue;
}

//-------------------------------------------------------------------------------------------------

/* What the clock setting is called on the row. The buffer is static because
 * the briefing holds pointers into whatever it is given and is redrawn every
 * frame from this same call. */
static const char *mecha_mode_round_time_name(void)
{
  static char szName[16];
  int iSeconds = s_aiRoundSeconds[s_iRoundChoice % MECHA_ROUND_CHOICES];

  if (iSeconds <= 0)
    return "DEATHMATCH";
  snprintf(szName, sizeof(szName), "%d SECONDS", iSeconds);
  return szName;
}

//-------------------------------------------------------------------------------------------------

/* Builds the briefing exactly as it will be drawn. Kept here rather than in
 * the renderer so the mode owns what its own settings are called. */
static void mecha_mode_build_briefing(tMechaBriefing *pBrief)
{
  memset(pBrief, 0, sizeof(*pBrief));
  pBrief->szResult = s_szLastResult;
  pBrief->bResultWin = s_bLastResultWin;
  pBrief->iSelection = s_iBriefSelection;
  pBrief->iRowCount = MECHA_ROW_COUNT;

  pBrief->aRows[MECHA_ROW_START].szLabel = "START MATCH";
  pBrief->aRows[MECHA_ROW_MODE].szLabel = "GAME MODE";
  pBrief->aRows[MECHA_ROW_MODE].szValue = mecha_mode_game_name(s_iGameMode);
  pBrief->aRows[MECHA_ROW_MECH].szLabel = "YOUR MECH";
  pBrief->aRows[MECHA_ROW_MECH].szValue = mecha_mode_mech_name(s_iPlayerDef);
  pBrief->aRows[MECHA_ROW_COLOURS].szLabel = "PAINT";
  pBrief->aRows[MECHA_ROW_COLOURS].szValue = mecha_scheme_name(s_iScheme);
  pBrief->aRows[MECHA_ROW_OPPONENT].szLabel = "OPPONENT";
  pBrief->aRows[MECHA_ROW_OPPONENT].szValue =
      mecha_mode_mech_name(s_iOpponentDef);
  pBrief->aRows[MECHA_ROW_ARENA].szLabel = "ARENA";
  pBrief->aRows[MECHA_ROW_ARENA].szValue = mecha_mode_arena_name(s_iArenaIdx);
  pBrief->aRows[MECHA_ROW_SKILL].szLabel = "OPPONENT SKILL";
  pBrief->aRows[MECHA_ROW_SKILL].szValue = mecha_mode_skill_name(s_iAiSkill);
  pBrief->aRows[MECHA_ROW_TIME].szLabel = "ROUND TIME";
  pBrief->aRows[MECHA_ROW_TIME].szValue = mecha_mode_round_time_name();
  pBrief->aRows[MECHA_ROW_HOLD_FIRE].szLabel = "ENEMY WEAPONS";
  pBrief->aRows[MECHA_ROW_HOLD_FIRE].szValue = s_bAiHoldFire
                                                 ? "HELD - DEBUG" : "LIVE";
  pBrief->aRows[MECHA_ROW_SPECTATE].szLabel = "SPECTATOR";
  pBrief->aRows[MECHA_ROW_SPECTATE].szValue = s_bSpectate ? "FREE CAMERA"
                                                          : "OFF";
  pBrief->aRows[MECHA_ROW_CONTROLS].szLabel = "VIEW CONTROLS";
  pBrief->aRows[MECHA_ROW_EXIT].szLabel = mecha_mode_retail_present()
                                           ? "EXIT TO WHIPLASH" : "QUIT";
}

//-------------------------------------------------------------------------------------------------

/*
 * The spectator camera is ROLLER's own free camera: the same mouse look,
 * the same WASD and the same speed multipliers the track's noclip uses. It
 * keeps its state in the track frame, where Z is up, so the arena maps on
 * the way in and on the way back out -- (x, y, z) there is (x, z, y) here,
 * and its yaw is measured from a different axis, so a quarter turn
 * separates them. [MODE-05]
 */
static void mecha_mode_free_camera_place(void)
{
  const tMechaArena *pArena = &s_World.arena;
  float fBack = pArena->fHalfExtent * 0.55f;

  s_Camera.fX = 0.0f;
  s_Camera.fY = pArena->fHalfExtent * 0.30f;
  s_Camera.fZ = -fBack;
  s_Camera.iYaw = 0;
  s_Camera.iPitch = -MECHA_DEG(14);
  s_Camera.bSettled = true;
  noclip_camera_place(s_Camera.fX, s_Camera.fZ, s_Camera.fY,
                      MECHA_ANGLE_QUARTER - s_Camera.iYaw, s_Camera.iPitch);
}

static void mecha_mode_free_camera_update(void)
{
  float fX = 0.0f;
  float fY = 0.0f;
  float fZ = 0.0f;
  int iYaw = 0;
  int iPitch = 0;

  noclip_camera_update();
  noclip_camera_get(&fX, &fY, &fZ, &iYaw, &iPitch);
  s_Camera.fX = fX;
  s_Camera.fY = fZ;
  s_Camera.fZ = fY;
  s_Camera.iYaw = mecha_angle_wrap(MECHA_ANGLE_QUARTER - iYaw);
  s_Camera.iPitch = iPitch;
}

//-------------------------------------------------------------------------------------------------

static void mecha_mode_start_match(void)
{
  int iSeat;

  mecha_sim_init(&s_World, s_iArenaIdx, (uint32)SDL_GetTicksNS() | 1u,
                 s_iRoundsToWin);
  mecha_sim_set_ai_skill(&s_World, s_iAiSkill);
  mecha_sim_set_round_seconds(&s_World,
                              s_aiRoundSeconds[s_iRoundChoice
                                               % MECHA_ROUND_CHOICES]);
  mecha_sim_set_ai_hold_fire(&s_World, s_bAiHoldFire);
  /*
   * Everyone on their own team, so a free-for-all is genuinely free: the
   * simulation only ever asks whether two machines share a team, and no two
   * of these do. A spectator's chosen machine still takes the field with a
   * computer pilot in it: leaving the seat empty would put a single machine
   * in a duel, and a round with one team in it can never end. [MODE-04]
   */
  iSeat = mecha_sim_add_mech(&s_World, s_iPlayerDef,
                             s_bSpectate ? MECHA_CONTROL_AI
                                         : MECHA_CONTROL_HUMAN, 0);
  s_iPlayerIdx = s_bSpectate ? -1 : iSeat;
  if (s_iGameMode == MECHA_GAME_SURVIVAL) {
    int iSlot;

    for (iSlot = 0; iSlot < MECHA_MAX_MECHS; iSlot++)
      if (mecha_sim_add_mech(&s_World,
                             (s_iOpponentDef + iSlot) % mecha_mode_mech_count(),
                             MECHA_CONTROL_AI, (uint8)(iSlot + 1)) < 0)
        break;
  } else {
    mecha_sim_add_mech(&s_World, s_iOpponentDef, MECHA_CONTROL_AI, 1);
  }
  if (iSeat >= 0)
    s_World.aMechs[iSeat].byScheme = (uint8_t)s_iScheme;
  mecha_sim_begin_match(&s_World);

  mecha_camera_reset(&s_Camera);
  if (s_iPlayerIdx >= 0) {
    mecha_camera_update(&s_Camera, &s_World, s_iPlayerIdx);
  } else {
    /* Nobody to chase, so the camera is the player's. */
    g_bNoclip = true;
    mecha_mode_free_camera_place();
  }

  /* The briefing's music stops with the briefing. */
  mecha_sound_match();
  s_ullResultHoldNs = 0;
  s_ullLastTimeNs = SDL_GetTicksNS();
  s_ullAccumulatorNs = 0;
  s_eScreen = MECHA_SCREEN_MATCH;
  /* Whichever key or button started the match is still down. */
  s_bQuitHeld = true;

  SDL_Log("arena: match started (%s vs %s, %s, skill %s, first to %d, %s%s)",
          mecha_mode_mech_name(s_iPlayerDef),
          mecha_mode_mech_name(s_iOpponentDef),
          mecha_mode_arena_name(s_iArenaIdx),
          mecha_sim_ai_skill_name(s_iAiSkill), s_iRoundsToWin,
          mecha_mode_round_time_name(),
          s_bAiHoldFire ? ", enemy weapons held" : "");
}

//-------------------------------------------------------------------------------------------------

/* Back to the briefing, carrying how the match ended. */
static void mecha_mode_return_to_briefing(const char *szResult, bool bWin)
{
  s_szLastResult = szResult;
  s_bLastResultWin = bWin;
  /* The free camera goes back where it was found: it grabs the mouse while
   * it runs, and the briefing needs the pointer back. */
  if (g_bNoclip) {
    g_bNoclip = false;
    noclip_camera_reset();
  }
  mecha_sound_briefing();
  s_eScreen = MECHA_SCREEN_BRIEFING;
  s_iBriefSelection = MECHA_ROW_START;
  s_ullResultHoldNs = 0;
  /* Whatever ended the match is still held. */
  s_bConfirmHeld = true;
  s_bBackHeld = true;
  memset(&s_Up, 0, sizeof(s_Up));
  memset(&s_Down, 0, sizeof(s_Down));
  memset(&s_Left, 0, sizeof(s_Left));
  memset(&s_Right, 0, sizeof(s_Right));
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

  /* g_pGameRenderer is built by play_game_init(), which --arena never runs,
   * so the mode stands one up itself. [MODE-02] */
  s_bCreatedRenderer = false;
  if (!g_pGameRenderer) {
    g_pGameRenderer = game_render_create(ROLLERGetGPUDevice(),
                                         ROLLERGetWindow());
    s_bCreatedRenderer = g_pGameRenderer != NULL;
  }
  if (!g_pGameRenderer) {
    SDL_Log("arena: could not create a renderer; returning to the menu");
    eFrontendNextState = eFRONTEND_STATE_MAIN_MENU;
    return;
  }

  /* The mode is built around the software rasteriser: it sorts its own
   * geometry back to front because that path has no depth buffer, and it
   * writes the HUD straight into the indexed frame. Forcing the mode here
   * means it behaves the same whatever the player left the renderer set to. */
  s_ePreviousRenderMode = game_render_get_mode(g_pGameRenderer);
  game_render_set_mode(g_pGameRenderer, GAME_RENDER_SOFTWARE);

  /*
   * Only supply a palette when nothing has loaded one. The mode's indices
   * were picked to match the game's own PALETTE.PAL, so a player with the
   * retail data should see it through their palette, not a substitute.
   */
  s_pSavedPalAddr = pal_addr;
  s_bPaletteInstalled = false;
  /* The game's own palette first when it is installed: retail tiles are
   * drawn in its indices, and the fallback turns them to noise.
   * [MODE-01] */
  if (!mecha_mode_palette_loaded() && mecha_file_present("palette.pal")) {
    /*
     * setpal owns pal_addr and frees it. Do not repoint it at the static
     * array afterwards: the next setpal free()s that and aborts.
     * [MODE-01]
     */
    setpal("palette.pal");
    FindShades();
  }
  if (!mecha_mode_palette_loaded()) {
    /* Whatever is current now, which is not necessarily what was current on
     * the way in: a setpal that got as far as freeing and then failed to
     * load leaves the old pointer dangling, and putting that back on the
     * way out would hand the next setpal a pointer to free twice. */
    s_pSavedPalAddr = pal_addr;
    memcpy(s_aSavedPalette, palette, sizeof(s_aSavedPalette));
    mecha_render_build_palette(s_aArenaPalette);
    memcpy(palette, s_aArenaPalette, sizeof(palette));
    /* The mode's table is static and setpal would free it, so the selector
     * goes to -1 while it is installed. Both restored on exit.
     * [MODE-01] */
    s_pSavedPalSelector = pal_selector;
    pal_addr = s_aArenaPalette;
    pal_selector = (void *)-1;
    /* Derives shade_palette from palette[], which is what shadow_poly reads
     * for mech shadows and ground dust. */
    FindShades();
    game_render_set_palette(g_pGameRenderer, s_aArenaPalette);
    s_bPaletteInstalled = true;
  }

  mecha_render_init_assets(g_pGameRenderer);
  mecha_sound_enter();
  mecha_sound_briefing();

  /* The briefing first, always. Coming straight in on --arena would
   * otherwise drop a player into a fight without ever having been told
   * which keys do what, and leave no way back to the race but the window
   * close button. */
  s_eScreen = MECHA_SCREEN_BRIEFING;
  s_iBriefSelection = MECHA_ROW_START;
  s_szLastResult = NULL;
  s_bLastResultWin = false;
  s_ullResultHoldNs = 0;
  s_iPlayerIdx = -1;
  memset(&s_World, 0, sizeof(s_World));
  memset(&s_Up, 0, sizeof(s_Up));
  memset(&s_Down, 0, sizeof(s_Down));
  memset(&s_Left, 0, sizeof(s_Left));
  memset(&s_Right, 0, sizeof(s_Right));

  SDL_Log("arena: entered (renderer=%p scrbuf=%p frame=%dx%d palette=%s)",
          (void *)g_pGameRenderer, (void *)scrbuf, XMAX, YMAX,
          s_bPaletteInstalled ? "built-in fallback" : "game's own");

  s_ullLastTimeNs = SDL_GetTicksNS();
  s_ullAccumulatorNs = 0;
  /* Whatever key or button sent us here may still be down. */
  s_bQuitHeld = true;
  s_bConfirmHeld = true;
  s_bBackHeld = true;
  s_bActive = true;
}

//-------------------------------------------------------------------------------------------------

/* The briefing: choose the match, or leave. */
static void mecha_mode_update_briefing(uint64 ullNowNs)
{
  tMechaMenuInput menu;
  bool bConfirm;
  bool bBack;
  int iStep = 0;

  mecha_input_poll_menu(&menu);

  if (mecha_mode_repeat(&s_Up, menu.bUp, ullNowNs))
    s_iBriefSelection = mecha_mode_wrap(s_iBriefSelection - 1,
                                        MECHA_ROW_COUNT);
  if (mecha_mode_repeat(&s_Down, menu.bDown, ullNowNs))
    s_iBriefSelection = mecha_mode_wrap(s_iBriefSelection + 1,
                                        MECHA_ROW_COUNT);
  if (mecha_mode_repeat(&s_Left, menu.bLeft, ullNowNs))
    iStep -= 1;
  if (mecha_mode_repeat(&s_Right, menu.bRight, ullNowNs))
    iStep += 1;

  if (iStep != 0) {
    switch (s_iBriefSelection) {
    case MECHA_ROW_MECH:
      s_iPlayerDef = mecha_mode_wrap(s_iPlayerDef + iStep,
                                     mecha_mode_mech_count());
      break;
    case MECHA_ROW_COLOURS:
      s_iScheme = mecha_mode_wrap(s_iScheme + iStep, mecha_scheme_count());
      break;
    case MECHA_ROW_OPPONENT:
      s_iOpponentDef = mecha_mode_wrap(s_iOpponentDef + iStep,
                                       mecha_mode_mech_count());
      break;
    case MECHA_ROW_ARENA:
      s_iArenaIdx = mecha_mode_wrap(s_iArenaIdx + iStep,
                                    mecha_mode_arena_count());
      break;
    case MECHA_ROW_SKILL:
      s_iAiSkill = mecha_mode_wrap(s_iAiSkill + iStep, MECHA_AI_SKILL_COUNT);
      break;
    case MECHA_ROW_TIME:
      s_iRoundChoice = mecha_mode_wrap(s_iRoundChoice + iStep,
                                       MECHA_ROUND_CHOICES);
      break;
    case MECHA_ROW_HOLD_FIRE:
      /* Two states, so either direction is the same toggle. */
      s_bAiHoldFire = !s_bAiHoldFire;
      break;
    case MECHA_ROW_MODE:
      s_iGameMode = mecha_mode_wrap(s_iGameMode + iStep, MECHA_GAME_COUNT);
      break;
    case MECHA_ROW_SPECTATE:
      s_bSpectate = !s_bSpectate;
      break;
    default:
      break;
    }
  }

  bConfirm = menu.bConfirm;
  bBack = menu.bBack;

  /* Escape leaves the arena from here, which is the same thing the exit row
   * does -- there is nothing above this screen to back out to. */
  if ((bBack && !s_bBackHeld)
      || (bConfirm && !s_bConfirmHeld
          && s_iBriefSelection == MECHA_ROW_EXIT)) {
    s_bBackHeld = bBack;
    s_bConfirmHeld = bConfirm;
    /* Back to the game when there is one, and out of the process when there
     * is not -- the menus cannot come up without the retail data, so sending
     * anyone there would only crash somewhere less obvious. */
    if (mecha_mode_retail_present()) {
      SDL_Log("arena: leaving for the main menu");
      frontend_set_state(eFRONTEND_STATE_MAIN_MENU);
    } else {
      SDL_Log("arena: no retail data present, quitting");
      frontend_set_state(eFRONTEND_STATE_QUIT);
    }
    return;
  }

  if (bConfirm && !s_bConfirmHeld) {
    if (s_iBriefSelection == MECHA_ROW_START)
      mecha_mode_start_match();
    else if (s_iBriefSelection == MECHA_ROW_CONTROLS)
      s_eScreen = MECHA_SCREEN_CONTROLS;
  }

  s_bConfirmHeld = bConfirm;
  s_bBackHeld = bBack;
}

//-------------------------------------------------------------------------------------------------

/* The controls page: nothing to choose, so anything that means yes or no
 * goes back to the briefing. */
static void mecha_mode_update_controls(void)
{
  tMechaMenuInput menu;

  mecha_input_poll_menu(&menu);
  if ((menu.bConfirm && !s_bConfirmHeld) || (menu.bBack && !s_bBackHeld))
    s_eScreen = MECHA_SCREEN_BRIEFING;
  s_bConfirmHeld = menu.bConfirm;
  s_bBackHeld = menu.bBack;
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

  if (s_eScreen == MECHA_SCREEN_CONTROLS) {
    mecha_mode_update_controls();
    s_ullLastTimeNs = SDL_GetTicksNS();
    s_ullAccumulatorNs = 0;
    return;
  }

  if (s_eScreen == MECHA_SCREEN_BRIEFING) {
    mecha_mode_update_briefing(SDL_GetTicksNS());
    /* The clock keeps moving while the briefing is up; without this the
     * first frame of the match would try to catch up on all of it. */
    s_ullLastTimeNs = SDL_GetTicksNS();
    s_ullAccumulatorNs = 0;
    return;
  }

  /* Escape backs out of a match to the briefing rather than all the way to
   * the race, which is where the exit actually lives. */
  bQuit = mecha_input_quit_pressed();
  if (bQuit && !s_bQuitHeld) {
    s_bQuitHeld = true;
    mecha_mode_return_to_briefing(NULL, false);
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
  else
    mecha_mode_free_camera_update();

  /* After the camera: the listener stands where the frame is drawn from. */
  mecha_sound_update(&s_World, &s_Camera);

  /* A decided match holds on VICTORY or DEFEAT long enough to be read, then
   * hands the player back to the briefing. The simulation keeps ticking
   * through it so the last blast plays out. */
  if (s_World.match.byPhase == MECHA_PHASE_MATCH_OVER) {
    s_ullResultHoldNs += ullElapsed;
    if (s_ullResultHoldNs >= MECHA_RESULT_HOLD_NS) {
      int iWinner = s_World.match.iWinnerIdx;
      bool bWin = iWinner >= 0 && iWinner == s_iPlayerIdx;

      if (iWinner < 0) {
        mecha_mode_return_to_briefing("LAST MATCH:  DRAW", false);
      } else if (s_iPlayerIdx < 0) {
        /* Nothing was won or lost from the free camera, so the briefing
         * names whoever took it instead. */
        snprintf(s_szSpectateResult, sizeof(s_szSpectateResult),
                 "LAST MATCH:  %s",
                 mecha_mode_mech_name((int)s_World.aMechs[iWinner].byDefIdx));
        mecha_mode_return_to_briefing(s_szSpectateResult, false);
      } else {
        mecha_mode_return_to_briefing(bWin ? "LAST MATCH:  VICTORY"
                                           : "LAST MATCH:  DEFEAT", bWin);
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------

void mecha_mode_draw(void)
{
  if (!s_bActive || !scrbuf)
    return;

  if (s_eScreen == MECHA_SCREEN_CONTROLS) {
    mecha_render_controls(scrbuf, XMAX, YMAX);
    return;
  }

  if (s_eScreen == MECHA_SCREEN_BRIEFING) {
    tMechaBriefing brief;

    mecha_mode_build_briefing(&brief);
    game_render_begin_frame(g_pGameRenderer);
    mecha_render_briefing(&brief, scrbuf, XMAX, YMAX);
    game_render_end_frame(g_pGameRenderer);
    return;
  }

  /* A spectator has no machine, and the frame is drawn with no view mech at
   * all: the scene is the same, the HUD is somebody else's business. Not
   * drawing it at all leaves the last briefing frame on screen, which reads
   * as a hung game. [MODE-07] */
  game_render_begin_frame(g_pGameRenderer);
  mecha_render_frame(g_pGameRenderer, &s_World, &s_Camera, s_iPlayerIdx,
                     scrbuf, XMAX, YMAX, s_aQuads, MECHA_QUAD_CAPACITY);
  game_render_end_frame(g_pGameRenderer);
}

//-------------------------------------------------------------------------------------------------

/*
 * The exit path as a headless scene: leaving the arena is the one thing here
 * that cannot be tested from the mode's own side, because what it has to
 * leave working is somebody else's screen.
 */
void snapshot_render_arena_exit(void)
{
  mecha_mode_enter();
  mecha_mode_exit();
  frontend_menu_enter();
  frontend_menu_update();
  snapshot_render_menu_main();
}

//-------------------------------------------------------------------------------------------------

void mecha_mode_exit(void)
{
  if (!s_bActive)
    return;

  SDL_Log("arena: exiting");
  mecha_sound_exit();
  /* The renderer stays: tearing it down nulls g_pGameRenderer, which is what
   * the menus and the race then reach for. [MODE-02] */
  s_bCreatedRenderer = false;
  if (g_pGameRenderer)
    game_render_set_mode(g_pGameRenderer, s_ePreviousRenderMode);

  if (s_bPaletteInstalled) {
    memcpy(palette, s_aSavedPalette, sizeof(palette));
    pal_addr = s_pSavedPalAddr;
    pal_selector = s_pSavedPalSelector;
    s_bPaletteInstalled = false;
  }

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
