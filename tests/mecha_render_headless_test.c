/*
 * Headless proof that the arena mode actually rasterises.
 *
 * mecha_sim_test.c covers the simulation, which needs nothing but libc. This
 * one covers the other half: it builds a real GameRenderer in software mode
 * with no GPU device and no window, renders arena frames into an indexed
 * buffer, and asserts that the geometry, the effects and the HUD all reach
 * pixels. That is the part no unit test and no compile check can speak for.
 *
 * Given an output directory it also writes the frames out as indexed PNGs,
 * using a stand-in palette (the real one lives in the retail data), so the
 * layout can be looked at rather than only asserted about.
 */
#include "3d.h"
#include "game_render.h"
#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_mesh.h"
#include "mecha_render.h"
#include "mecha_sim.h"
#include "func2.h"
#include "png_writer.h"
#include "sound.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_W 640
#define FRAME_H 400

static int check(int bCondition, int iLine)
{
    if (!bCondition)
        fprintf(stderr, "mecha render check failed at line %d\n", iLine);
    return bCondition ? 0 : iLine;
}

#define CHECK(condition) \
    do { \
        int iResult = check((condition), __LINE__); \
        if (iResult != 0) \
            return iResult; \
    } while (0)

static uint8 s_aFrame[FRAME_W * FRAME_H];
static tMechaQuad s_aQuads[MECHA_QUAD_CAPACITY];
static tMechaWorld s_World;
static tMechaCamera s_Camera;

/* How many pixels carry each palette index. */
static void histogram(const uint8 *pFrame, int aiCounts[256])
{
    memset(aiCounts, 0, sizeof(int) * 256);
    for (size_t i = 0; i < (size_t)FRAME_W * FRAME_H; i++)
        aiCounts[pFrame[i]]++;
}

static int distinct_colours(const int aiCounts[256])
{
    int iCount = 0;

    for (int i = 0; i < 256; i++) {
        if (aiCounts[i] > 0)
            iCount++;
    }
    return iCount;
}

/*
 * The palette the mode actually installs at runtime. Using the shipping
 * table rather than a stand-in is the point: an earlier version of this test
 * invented its own colours, which meant it happily passed while the real
 * mode presented a black screen because nothing had filled pal_addr.
 */
static void build_preview_palette(tColor *paPalette)
{
    mecha_render_build_palette(paPalette);
}

static void dump_frame(const char *szOutDir, const char *szName)
{
    static tColor aPalette[256];
    char szPath[512];

    if (!szOutDir)
        return;
    build_preview_palette(aPalette);
    snprintf(szPath, sizeof(szPath), "%s/%s", szOutDir, szName);
    if (RollerWriteIndexedPng(szPath, s_aFrame, aPalette, FRAME_W, FRAME_H) == 0)
        printf("   wrote %s\n", szPath);
    else
        fprintf(stderr, "   FAILED to write %s\n", szPath);
}

static void render_now(GameRenderer *pRenderer, int iViewMech)
{
    mecha_camera_update(&s_Camera, &s_World, iViewMech);
    mecha_render_frame(pRenderer, &s_World, &s_Camera, iViewMech,
                       s_aFrame, FRAME_W, FRAME_H,
                       s_aQuads, MECHA_QUAD_CAPACITY);
}

int main(int argc, char **argv)
{
    const char *szOutDir = argc > 1 ? argv[1] : NULL;
    GameRenderer *pRenderer;
    int aiCounts[256];
    int iPlayer;
    int iSkyOnly;

    SDL_SetMainReady();

    /* Software mode needs neither a GPU device nor a window, which is the
     * whole reason this test can run on a headless runner. */
    pRenderer = game_render_create(NULL, NULL);
    CHECK(pRenderer != NULL);
    game_render_set_mode(pRenderer, GAME_RENDER_SOFTWARE);
    CHECK(game_render_get_mode(pRenderer) == GAME_RENDER_SOFTWARE);

    /* Same palette install the mode performs, so the frames this test
     * rasterises are shaded the way the real thing would be. */
    {
        static tColor aPalette[256];

        mecha_render_build_palette(aPalette);
        memcpy(palette, aPalette, sizeof(palette));
        pal_addr = aPalette;
        FindShades();
    }

    mecha_sim_init(&s_World, 0, 0x5EED1234u, 2);
    iPlayer = mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_HUMAN, 0);
    CHECK(iPlayer >= 0);
    CHECK(mecha_sim_add_mech(&s_World, 1, MECHA_CONTROL_AI, 1) >= 0);
    mecha_sim_begin_match(&s_World);
    mecha_camera_reset(&s_Camera);

    /* --- an empty frame still has to draw the world ---------------------- */

    render_now(pRenderer, iPlayer);
    histogram(s_aFrame, aiCounts);
    dump_frame(szOutDir, "arena_ready.png");

    /* If the sky fill were the only thing written, nothing rasterised. */
    iSkyOnly = aiCounts[s_World.arena.bySkyPalette] == FRAME_W * FRAME_H;
    CHECK(!iSkyOnly);
    CHECK(distinct_colours(aiCounts) >= 6);

    /* The arena itself: sky, both checkerboard tones, and the walls. */
    CHECK(aiCounts[s_World.arena.bySkyPalette] > 0);
    CHECK(aiCounts[s_World.arena.byFloorPalette] > 0);
    CHECK(aiCounts[s_World.arena.byGridPalette] > 0);
    CHECK(aiCounts[s_World.arena.byWallPalette] > 0);

    /* The HUD: bar frames and text both reach pixels. These mirror
     * MECHA_HUD_FRAME and MECHA_HUD_TEXT, which are private to
     * mecha_render.c; the palette assertions below are what actually guard
     * against them drifting apart. */
    CHECK(aiCounts[115] > 0);
    CHECK(aiCounts[143] > 0);

    /*
     * Every index the mode paints with must have a colour of its own.
     * Checked forwards, from the constants, rather than backwards from the
     * frame: shadow_poly emits indices out of the shade table that this mode
     * never chose, so "everything on screen is one of ours" is not true and
     * asserting it only produces false failures.
     */
    CHECK(mecha_render_palette_defines(s_World.arena.bySkyPalette));
    CHECK(mecha_render_palette_defines(s_World.arena.byFloorPalette));
    CHECK(mecha_render_palette_defines(s_World.arena.byGridPalette));
    CHECK(mecha_render_palette_defines(s_World.arena.byWallPalette));
    for (int iDef = 0; iDef < mecha_def_count(); iDef++) {
        const tMechaMechDef *pDef = mecha_def_get(iDef);

        for (int iHue = 0; iHue < 4; iHue++)
            CHECK(mecha_render_palette_defines(pDef->abyPalette[iHue]));
        for (int iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
            for (int iStance = 0; iStance < MECHA_STANCE_COUNT; iStance++)
                CHECK(mecha_render_palette_defines(
                    pDef->aWeapons[iSlot][iStance].byPalette));
        }
    }

    /* --- the player's own machine is on screen --------------------------- */
    {
        const tMechaMechDef *pDef = mecha_def_get(0);
        int iHull = aiCounts[pDef->abyPalette[0]]
                  + aiCounts[pDef->abyPalette[1]]
                  + aiCounts[pDef->abyPalette[2]];

        CHECK(iHull > 0);
    }

    /* --- fight for a while, then look again ------------------------------ */
    {
        tMechaInput aInputs[MECHA_MAX_MECHS];
        int aiLater[256];
        int i;
        int iTracer = 0;

        for (i = 0; i < MECHA_TICK_HZ * 4; i++) {
            memset(aInputs, 0, sizeof(aInputs));
            aInputs[iPlayer].iMoveZ = 100;
            aInputs[iPlayer].bDash = (i % 90) < 30;
            aInputs[iPlayer].bFireLeft = (i % 20) < 2;
            aInputs[iPlayer].bFireCenter = (i % 47) < 2;
            aInputs[iPlayer].bFireRight = (i % 71) < 2;
            mecha_sim_tick(&s_World, aInputs, MECHA_MAX_MECHS);
        }

        render_now(pRenderer, iPlayer);
        histogram(s_aFrame, aiLater);
        dump_frame(szOutDir, "arena_fight.png");

        CHECK(aiLater[s_World.arena.bySkyPalette] != FRAME_W * FRAME_H);
        CHECK(distinct_colours(aiLater) >= 6);

        /* Shots in flight have to be visible, or the tracer geometry is
         * being built and then thrown away. */
        iTracer = aiLater[231] + aiLater[207] + aiLater[171] + aiLater[243]
                + aiLater[219] + aiLater[195] + aiLater[183];
        CHECK(iTracer > 0);

        /* The camera followed the fight rather than staying put. */
        CHECK(memcmp(aiCounts, aiLater, sizeof(aiCounts)) != 0);
    }

    /* --- a knocked-down mech still renders ------------------------------- */
    {
        tMechaInput aInputs[MECHA_MAX_MECHS];
        int i;

        mecha_sim_damage(&s_World, iPlayer, 1, 10.0f,
                         MECHA_STAGGER_DOWN * 2.0f, 0.0f, 0.0f);
        memset(aInputs, 0, sizeof(aInputs));
        for (i = 0; i < 6; i++)
            mecha_sim_tick(&s_World, aInputs, MECHA_MAX_MECHS);
        render_now(pRenderer, iPlayer);
        histogram(s_aFrame, aiCounts);
        dump_frame(szOutDir, "arena_down.png");
        CHECK(aiCounts[s_World.arena.bySkyPalette] != FRAME_W * FRAME_H);
    }

    game_render_destroy(pRenderer);
    printf("mecha render: headless software frames rasterised\n");
    return 0;
}
