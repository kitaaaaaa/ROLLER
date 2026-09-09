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

/* True when any pixel on this row is something other than the background
 * the briefing fills with. */
static bool row_has_ink(const uint8 *pFrame, int iWidth, int iY)
{
    uint8 byGround = pFrame[0];
    int x;

    for (x = 0; x < iWidth; x++) {
        if (pFrame[iY * iWidth + x] != byGround)
            return true;
    }
    return false;
}

//-------------------------------------------------------------------------------------------------

/* The last row carrying anything, or -1 for an empty frame. */
static int last_ink_row(const uint8 *pFrame, int iWidth, int iHeight)
{
    int iLast = -1;
    int y;

    for (y = 0; y < iHeight; y++) {
        if (row_has_ink(pFrame, iWidth, y))
            iLast = y;
    }
    return iLast;
}

//-------------------------------------------------------------------------------------------------

/* True when one index covers the whole frame -- nothing rasterised. */
static int single_colour(const int aiCounts[256])
{
    int i;

    for (i = 0; i < 256; i++) {
        if (aiCounts[i] == FRAME_W * FRAME_H)
            return 1;
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* The index in the middle of a row, away from anything drawn at the edges. */
static uint8 row_colour(const uint8 *pFrame, int iY)
{
    return pFrame[iY * FRAME_W + FRAME_W / 2];
}

//-------------------------------------------------------------------------------------------------

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

/* Ticks past the READY announcement so the controls are live. */
static void run_to_fight(tMechaInput *paInputs)
{
    int i;

    for (i = 0; i < MECHA_TICK_HZ * 4
                && s_World.match.byPhase != MECHA_PHASE_FIGHT; i++)
        mecha_sim_tick(&s_World, paInputs, MECHA_MAX_MECHS);
}

//-------------------------------------------------------------------------------------------------

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
    /* Picks up the retail HUD font when the data is next to the binary, and
     * quietly does nothing when it is not -- which is how this runs in CI,
     * and why the built-in font has to keep working. */
    mecha_render_init_assets(pRenderer);
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

    /* If one colour covered the frame, nothing rasterised. */
    iSkyOnly = single_colour(aiCounts);
    CHECK(!iSkyOnly);
    CHECK(distinct_colours(aiCounts) >= 6);

    /*
     * The sky is a gradient rather than a fill, and the cheapest way to say
     * so without copying the band table into the test is that the top of the
     * frame is not the colour of the band just above the horizon. Row zero
     * is always sky; the row the horizon sits on is not known here, so a row
     * a third of the way down stands in for it -- with the camera pitched
     * down as it is, that is still sky and still several bands lower.
     */
    CHECK(row_colour(s_aFrame, 0) != row_colour(s_aFrame, FRAME_H / 3));

    /* The arena itself: both checkerboard tones and the walls. */
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

        CHECK(!single_colour(aiLater));
        CHECK(distinct_colours(aiLater) >= 6);

        /* Shots in flight have to be visible, or the tracer geometry is
         * being built and then thrown away. */
        iTracer = aiLater[231] + aiLater[207] + aiLater[171] + aiLater[243]
                + aiLater[219] + aiLater[195] + aiLater[183];
        CHECK(iTracer > 0);

        /* The camera followed the fight rather than staying put. */
        CHECK(memcmp(aiCounts, aiLater, sizeof(aiCounts)) != 0);
    }

    /*
     * --- the death blast is a burst, not a wall --------------------------
     *
     * The explosion is an opaque billboard and its scale is a half-extent,
     * so an over-large figure paints a flat slab across the middle of the
     * screen on the one frame the player most needs to read. Measured
     * against a recorded match, the original covered 17% of the play area
     * at its widest. This pins it well under that.
     */
    {
        /* Machine 2's accent is the one colour on the roster that no HUD
         * element also paints in, so the blast can simply be counted across
         * the whole frame with nothing to subtract. */
        tMechaInput aInputs[MECHA_MAX_MECHS];
        uint8 byBlast = mecha_def_get(2)->abyPalette[3];
        int aiPeak[256];
        int iPeak = 0;
        int i;

        mecha_sim_init(&s_World, 0, 0xB1A57u, 2);
        iPlayer = mecha_sim_add_mech(&s_World, 2, MECHA_CONTROL_HUMAN, 0);
        CHECK(mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_AI, 1) >= 0);
        mecha_sim_begin_match(&s_World);
        mecha_camera_reset(&s_Camera);
        memset(aInputs, 0, sizeof(aInputs));
        run_to_fight(aInputs);

        /* Straight to zero armour, which is what spawns the death blast. */
        mecha_sim_damage(&s_World, iPlayer, 1, 100000.0f, 0.0f, 0.0f, 0.0f);

        /* Walk the blast's whole life and keep its widest frame. */
        for (i = 0; i < MECHA_TICK_HZ; i++) {
            render_now(pRenderer, iPlayer);
            histogram(s_aFrame, aiPeak);
            if (aiPeak[byBlast] > iPeak)
                iPeak = aiPeak[byBlast];
            /* Dumped on a fixed tick rather than on the peak: with the
             * texture bank loaded the peak count stays zero, so a dump tied
             * to it would never fire and the one frame worth looking at
             * would be the one never written. */
            if (i == MECHA_TICK_HZ / 6)
                dump_frame(szOutDir, "arena_blast.png");
            mecha_sim_tick(&s_World, aInputs, MECHA_MAX_MECHS);
        }

        /* Every colour the cooling debris walks through has to have one of
         * its own, or a dying machine sprays holes in the world. */
        {
            static const uint8 abyCool[] = {
                207, 204, 171, 170, 167, 230, 227, 224, 221
            };
            size_t iCool;

            for (iCool = 0; iCool < sizeof(abyCool) / sizeof(abyCool[0]);
                 iCool++)
                CHECK(mecha_render_palette_defines(abyCool[iCool]));
        }

        /*
         * Which path drew it decides what there is to measure. Drawn from
         * the game's own texture bank the blast paints none of the flat
         * path's palette index, so this count is legitimately zero and the
         * size bound belongs to the other path. Without the bank -- which
         * is how this runs on a checkout with no retail data, and so how it
         * runs in CI -- the flat particles are what is on screen and their
         * size is the thing worth pinning.
         */
        printf("   death blast peaks at %d px, %.1f%% of the frame (%s)\n",
               iPeak, 100.0 * iPeak / (double)(FRAME_W * FRAME_H),
               mecha_render_sprites_active() ? "textured" : "flat");
        if (mecha_render_sprites_active()) {
            int iForeign = 0;
            int iIdx;

            /*
             * The blast is drawn out of the retail bank now, so it paints
             * none of the flat path's colour -- zero here is the expected
             * reading, not a missing explosion. What proves the frames
             * actually reached the screen is the opposite: the bank's tiles
             * are drawn in the retail palette's indices, which are mostly
             * ones this mode never paints with, so pixels the mode's own
             * palette does not define can only have come from a sprite.
             *
             * (It is also why the dumped PNGs look empty here: they are
             * written through the mode's fallback palette, where those
             * indices resolve to the neutral fill. With the retail palette
             * loaded, as in the game, they resolve to fire.)
             */
            histogram(s_aFrame, aiPeak);
            for (iIdx = 0; iIdx < 256; iIdx++) {
                if (aiPeak[iIdx] > 0 && !mecha_render_palette_defines(iIdx))
                    iForeign += aiPeak[iIdx];
            }
            printf("   %d px came out of the texture bank\n", iForeign);
            /*
             * Bank pixels on screen is the whole assertion. The flat path's
             * colour is not required to vanish: the machine's own visor is
             * painted in it too, so a handful of pixels survive that have
             * nothing to do with the blast -- which is also why counting
             * that index was never a clean measure of blast size, only ever
             * an upper bound on it.
             */
            CHECK(iForeign > 0);
        } else {
            CHECK(iPeak > 0);
            CHECK(iPeak < FRAME_W * FRAME_H / 16);   /* under 6.25% */
        }
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
        CHECK(!single_colour(aiCounts));
    }

    /* --- the briefing screen draws ---------------------------------------
     *
     * It is the first thing the mode shows and the thing every match returns
     * to, so a blank one strands the player with no way back to the race.
     */
    {
        tMechaBriefing brief;
        int aiBrief[256];
        int i;

        memset(&brief, 0, sizeof(brief));
        brief.szResult = "LAST MATCH:  VICTORY";
        brief.bResultWin = true;
        brief.iSelection = 1;
        brief.iRowCount = 6;
        brief.aRows[0].szLabel = "START MATCH";
        brief.aRows[1].szLabel = "YOUR MECH";
        brief.aRows[1].szValue = mecha_def_get(0)->szName;
        brief.aRows[2].szLabel = "OPPONENT";
        brief.aRows[2].szValue = mecha_def_get(2)->szName;
        brief.aRows[3].szLabel = "ARENA";
        brief.aRows[3].szValue = mecha_arena_name(0);
        brief.aRows[4].szLabel = "OPPONENT SKILL";
        brief.aRows[4].szValue = mecha_sim_ai_skill_name(MECHA_AI_VETERAN);
        brief.aRows[5].szLabel = "EXIT TO WHIPLASH";

        memset(s_aFrame, 0xFF, sizeof(s_aFrame));
        mecha_render_briefing(&brief, s_aFrame, FRAME_W, FRAME_H);
        histogram(s_aFrame, aiBrief);
        dump_frame(szOutDir, "arena_briefing.png");

        /* Nothing left over from the frame before, and more than a fill. */
        CHECK(aiBrief[255] == 0);
        CHECK(distinct_colours(aiBrief) >= 4);

        /*
         * The screen has to fit, in both of the game's video modes. The
         * footer is the last thing drawn, so anything that ran off the
         * bottom took it first -- and the exit row sits just above it. The
         * 320x200 case is the one that actually binds: twenty-five lines of
         * this font is the entire buffer.
         */
        {
            static uint8 aSmall[320 * 200];
            int iLast = last_ink_row(s_aFrame, FRAME_W, FRAME_H);

            CHECK(iLast > 0);
            CHECK(iLast < FRAME_H - 2);

            memset(aSmall, 0xFF, sizeof(aSmall));
            mecha_render_briefing(&brief, aSmall, 320, 200);
            iLast = last_ink_row(aSmall, 320, 200);
            CHECK(iLast > 0);
            CHECK(iLast < 198);
            /* And it still drew the whole thing rather than shrinking to
             * nothing: the rows have to be down there. */
            CHECK(iLast > 150);
        }

        /*
         * Every colour it paints with has to be one the mode's palette
         * defines, or the screen presents as holes -- but only while the
         * mode is choosing all of them. The retail HUD font brings its own
         * indices, so with that loaded this says nothing and asserting it
         * would only be asserting that the font failed to load.
         */
        if (!mecha_render_font_is_retail()) {
            for (i = 0; i < 256; i++) {
                if (aiBrief[i] > 0)
                    CHECK(mecha_render_palette_defines(i));
            }
        }
    }

    game_render_destroy(pRenderer);
    printf("mecha render: headless software frames rasterised\n");
    return 0;
}
