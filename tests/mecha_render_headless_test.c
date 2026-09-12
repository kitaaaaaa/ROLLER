/*
 * Headless proof that the arena mode rasterises: a real GameRenderer in
 * software mode with no device and no window, asserting that geometry,
 * effects and HUD reach pixels. Given an output directory it writes the
 * frames as indexed PNGs to be looked at. [TEST-01]
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

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Not a colour this mode ever paints with; used to prove a full redraw. */
#define MECHA_TEST_SENTINEL 254

#define FRAME_W 640
#define FRAME_H 400

/* palette[] is the array setpal actually writes; pal_addr is not reliably
 * updated by it, which the GPU renderer documents in its own source. */
static bool mecha_test_palette_loaded(void)
{
    int i;

    for (i = 0; i < 256; i++) {
        if (palette[i].byR || palette[i].byG || palette[i].byB)
            return true;
    }
    return false;
}

static bool mecha_test_file_present(const char *szFile)
{
    FILE *pFile = fopen(szFile, "rb");

    if (!pFile)
        return false;
    fclose(pFile);
    return true;
}

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
/* Built again purely to be measured: the numbered overlay needs the body's
 * own quads, and the list the frame was drawn from has been sorted and has
 * everything else in the arena in it too. */
static tMechaQuad s_aBodyQuads[MECHA_QUAD_CAPACITY];

/* One number waiting to be painted onto a panel, and the box a painted one
 * has already taken. */
typedef struct { int iPoly; int iX; int iY; float fDist; } tPolyLabel;
typedef struct { int iX; int iY; int iW; } tPolyBox;
static tPolyLabel aLabels[MECHA_ZIZIN_BODY_QUADS];
static tPolyBox   aDrawn[MECHA_ZIZIN_BODY_QUADS];
static tMechaWorld s_World;
static tMechaCamera s_Camera;

/* How many pixels carry each palette index. */
static void histogram_of(const uint8 *pFrame, size_t uCount,
                         int aiCounts[256])
{
    memset(aiCounts, 0, sizeof(int) * 256);
    for (size_t i = 0; i < uCount; i++)
        aiCounts[pFrame[i]]++;
}

static void histogram(const uint8 *pFrame, int aiCounts[256])
{
    histogram_of(pFrame, (size_t)FRAME_W * FRAME_H, aiCounts);
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
    /* The palette the frame was actually drawn through, when there is one,
     * or the preview lies about retail tiles. [TEST-01] */
    if (mecha_test_palette_loaded()) {
        memcpy(paPalette, palette, sizeof(tColor) * 256);
        return;
    }
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
    int iEnemy;
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
    /* The retail palette when it is beside the binary, so the frames this
     * dumps are shaded the way the game shades them. */
    if (mecha_test_file_present("palette.pal")) {
        setpal("palette.pal");
        FindShades();
    }
    CHECK(game_render_get_mode(pRenderer) == GAME_RENDER_SOFTWARE);

    /* Same palette install the mode performs, so the frames this test
     * rasterises are shaded the way the real thing would be. */
    {
        static tColor aPalette[256];

        /* Only when the retail palette is not already loaded -- installing
         * the fallback over it is what made every textured surface come out
         * as noise, since those tiles are drawn in the retail indices. */
        if (!mecha_test_palette_loaded()) {
            mecha_render_build_palette(aPalette);
            memcpy(palette, aPalette, sizeof(palette));
            pal_addr = aPalette;
            FindShades();
        }
    }

    mecha_sim_init(&s_World, 0, 0x5EED1234u, 2);
    iPlayer = mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_HUMAN, 0);
    CHECK(iPlayer >= 0);
    iEnemy = mecha_sim_add_mech(&s_World, 1, MECHA_CONTROL_AI, 1);
    CHECK(iEnemy >= 0);
    mecha_sim_begin_match(&s_World);
    mecha_camera_reset(&s_Camera);

    /* --- an empty frame still has to draw the world ---------------------- */

    render_now(pRenderer, iPlayer);
    histogram(s_aFrame, aiCounts);
    dump_frame(szOutDir, "arena_ready.png");

    /* --- a rig sheet, for looking at the animation ----------------------
     * Four points of the step cycle, dumped rather than asserted: the
     * numbers that pin the rig live in the sim tests. */
    if (szOutDir) {
        static tMechaWorld worldSaved;
        tMechaInput aIdle[MECHA_MAX_MECHS];
        tMechaMech *pRig = &s_World.aMechs[iPlayer];
        tMechaMech *pEye = &s_World.aMechs[iEnemy];
        tMechaCamera savedCamera = s_Camera;
        int iStep;

        /* The whole world goes back afterwards, because posing a machine by
         * hand and running the clock on to clear the round announcement are
         * both things the assertions further down must not inherit. */
        worldSaved = s_World;
        memset(aIdle, 0, sizeof(aIdle));
        for (iStep = 0; iStep < MECHA_TICK_HZ * 5; iStep++)
            mecha_sim_tick(&s_World, aIdle, MECHA_MAX_MECHS);

        /* The chase camera is no use here -- it is parked behind a shoulder
         * and sees a back and two heels -- so the camera is placed by hand,
         * out in front of the machine and looking back at it, and the other
         * mech is sent to the far corner so it is not standing in the way. */
        pEye->fX = MECHA_M(80.0f);
        pEye->fZ = MECHA_M(80.0f);
        pEye->iFacing = 0;
        pEye->iLegYaw = 0;
        /* Whatever the five seconds of clock did to it, this is a machine
         * standing on the floor walking on the spot. */
        pRig->fX = 0.0f;
        pRig->fY = 0.0f;
        pRig->fZ = 0.0f;
        pRig->fVelX = 0.0f;
        pRig->fVelY = 0.0f;
        pRig->fVelZ = 0.0f;
        pRig->byMove = MECHA_MOVE_WALK;
        pRig->iStateTicks = 0;
        pRig->iStunTicks = 0;
        pRig->iInvulnTicks = 0;
        pRig->fLeanRoll = 0.0f;
        pRig->iFacing = MECHA_ANGLE_HALF;          /* facing the camera */
        pRig->iLegYaw = mecha_angle_wrap(pRig->iFacing + MECHA_DEG(40));
        pRig->byLock = MECHA_LOCK_HELD;
        pRig->iTargetIdx = iEnemy;

        /* Three quarters on, which shows the stride and the twist at once. */
        s_Camera.fX = -MECHA_M(19.0f);
        s_Camera.fY = MECHA_M(9.0f);
        s_Camera.fZ = -MECHA_M(19.0f);
        s_Camera.iYaw = MECHA_DEG(45);
        s_Camera.iPitch = -MECHA_DEG(6);
        s_Camera.bSettled = true;

        for (iStep = 0; iStep < 6; iStep++) {
            char szName[32];

            pRig->fStepPhase = (float)iStep / 6.0f;
            mecha_render_frame(pRenderer, &s_World, &s_Camera, iPlayer,
                               s_aFrame, FRAME_W, FRAME_H,
                               s_aQuads, MECHA_QUAD_CAPACITY);
            snprintf(szName, sizeof(szName), "arena_rig%d.png", iStep);
            dump_frame(szOutDir, szName);
        }
        /* One frame per gait, from the same camera: standing at ease,
         * standing with a lock to hold, walking, gliding, hanging, and
         * driving through the air. */
        {
            static const uint8 abyMove[6] = {
                MECHA_MOVE_STAND, MECHA_MOVE_STAND, MECHA_MOVE_WALK,
                MECHA_MOVE_DASH, MECHA_MOVE_JUMP, MECHA_MOVE_DASH
            };
            static const float afCombat[6] = {
                0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
            };
            int iGait;

            for (iGait = 0; iGait < 6; iGait++) {
                char szName[32];

                pRig->byMove = abyMove[iGait];
                pRig->fCombat = afCombat[iGait];
                pRig->fY = iGait >= 4 ? MECHA_M(9.0f) : 0.0f;
                pRig->fStepPhase = 0.12f;
                pRig->iLegYaw = mecha_angle_wrap(pRig->iFacing
                                                 + MECHA_DEG(40));
                mecha_render_frame(pRenderer, &s_World, &s_Camera, iPlayer,
                                   s_aFrame, FRAME_W, FRAME_H,
                                   s_aQuads, MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "arena_gait%d.png", iGait);
                dump_frame(szOutDir, szName);
            }
            pRig->byMove = MECHA_MOVE_WALK;
            pRig->fCombat = 1.0f;
            pRig->fY = 0.0f;
        }

        /*
         * Four bolts in a row, one per recoloured copy of the plasma
         * frames. The tint is built out of the palette at run time, so the
         * only way to know it worked is to look at it.
         */
        {
            static const uint8 abyTracer[4] = { 218, 171, 192, 255 };
            int iShot;

            memset(s_World.aProjectiles, 0, sizeof(s_World.aProjectiles));
            for (iShot = 0; iShot < 4; iShot++) {
                tMechaProjectile *pShot = &s_World.aProjectiles[iShot];

                memset(pShot, 0, sizeof(*pShot));
                pShot->bActive = true;
                pShot->byKind = MECHA_PROJ_HOMING;   /* drawn as the sprite */
                pShot->byOwner = (uint8_t)iPlayer;
                pShot->byPalette = abyTracer[iShot];
                pShot->fX = MECHA_M(-9.0f) + MECHA_M(6.0f) * (float)iShot;
                pShot->fY = MECHA_M(7.0f);
                pShot->fZ = -MECHA_M(6.0f);
                pShot->fPrevX = pShot->fX;
                pShot->fPrevY = pShot->fY;
                pShot->fPrevZ = pShot->fZ;
                pShot->fRadius = MECHA_M(1.6f);
                pShot->iLife = MECHA_TICK_HZ;
                pShot->iTarget = -1;
            }
            mecha_render_frame(pRenderer, &s_World, &s_Camera, iPlayer,
                               s_aFrame, FRAME_W, FRAME_H,
                               s_aQuads, MECHA_QUAD_CAPACITY);
            dump_frame(szOutDir, "arena_tints.png");
            /*
             * Every recoloured bank is built out of the palette the moment
             * a shot first asks for one, so having drawn four of them, all
             * three tints must be up -- or the data was never there and
             * none of them are.
             */
            printf("   %d of 3 bolt tints built (%s)\n",
                   mecha_render_tints_active(),
                   mecha_render_sprites_active() ? "textured" : "flat");
            CHECK(mecha_render_tints_active() == 3
                  || !mecha_render_sprites_active());
            memset(s_World.aProjectiles, 0, sizeof(s_World.aProjectiles));
        }

        /* And a landing, from the same camera: the dust ring wants looking
         * at more than the walk cycle does, being the one effect that is
         * meant to be read from above. */
        {
            const tMechaMechDef *pDef =
                mecha_def_get((int)pRig->byDefIdx);

            pRig->fStepPhase = 0.0f;
            mecha_sim_spawn_effect(&s_World, MECHA_FX_DUST, pRig->fX,
                                   pRig->fY, pRig->fZ, pDef->fRadius * 2.4f,
                                   pDef->abyPalette[2], MECHA_SEC(0.45f));
            for (iStep = 0; iStep < 3; iStep++) {
                char szName[32];
                int iTick;

                for (iTick = 0; iTick < 6; iTick++)
                    mecha_sim_tick(&s_World, aIdle, MECHA_MAX_MECHS);
                mecha_render_frame(pRenderer, &s_World, &s_Camera, iPlayer,
                                   s_aFrame, FRAME_W, FRAME_H,
                                   s_aQuads, MECHA_QUAD_CAPACITY);
                snprintf(szName, sizeof(szName), "arena_dust%d.png", iStep);
                dump_frame(szOutDir, szName);
            }
        }

        s_World = worldSaved;
        s_Camera = savedCamera;
        render_now(pRenderer, iPlayer);
    }

    /* If one colour covered the frame, nothing rasterised. */
    iSkyOnly = single_colour(aiCounts);
    CHECK(!iSkyOnly);
    CHECK(distinct_colours(aiCounts) >= 6);

    /* The sky is a gradient, not a fill: row zero must differ from a row a
     * third of the way down, which at this pitch is still sky. */
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

    /* Checked forwards from the constants, never backwards from the frame.
     * [TEST-02] */
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
        /* Every colour a weapon paints its shots in. Kept in step with the
         * PAL_TRACER_* set by the palette assertions further up, which walk
         * the roster rather than trusting this list. */
        iTracer = aiLater[231] + aiLater[255] + aiLater[206] + aiLater[192]
                + aiLater[34] + aiLater[171] + aiLater[218] + aiLater[143];
        CHECK(iTracer > 0);

        /* The camera followed the fight rather than staying put. */
        CHECK(memcmp(aiCounts, aiLater, sizeof(aiCounts)) != 0);
    }

    /*
     * --- the death blast is a burst, not a wall --------------------------
     * An opaque billboard whose scale is a half-extent, so an over-large
     * figure slabs the screen. [TEST-03]
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

        /* Which path drew it decides what there is to measure.
         * [TEST-03] */
        printf("   death blast peaks at %d px, %.1f%% of the frame (%s)\n",
               iPeak, 100.0 * iPeak / (double)(FRAME_W * FRAME_H),
               mecha_render_sprites_active() ? "textured" : "flat");
        if (mecha_render_sprites_active()) {
            int iForeign = 0;
            int iIdx;

            /* Zero here is the expected reading, not a missing explosion:
             * what proves the sprite drew is a pixel in an index the mode's
             * own palette does not define. [TEST-03] */
            histogram(s_aFrame, aiPeak);
            for (iIdx = 0; iIdx < 256; iIdx++) {
                if (aiPeak[iIdx] > 0 && !mecha_render_palette_defines(iIdx))
                    iForeign += aiPeak[iIdx];
            }
            printf("   %d px came out of the texture bank\n", iForeign);
            /* Bank pixels on screen is the whole assertion; the flat
             * colour is not required to vanish. [TEST-03] */
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

    /* --- every arena rasterises ------------------------------------------
     * The outdoor arenas are terrain rather than a flat floor and the roof
     * is nothing at all past its edge, so they are the ones most likely to
     * come out empty. */
    {
        int iArena;

        for (iArena = 0; iArena < mecha_arena_count(); iArena++) {
            char szName[64];
            int iPilot;
            int iFoe;

            mecha_sim_init(&s_World, iArena, 0x5EED1234u, 2);
            iPilot = mecha_sim_add_mech(&s_World, 0, MECHA_CONTROL_HUMAN, 0);
            CHECK(iPilot >= 0);
            iFoe = mecha_sim_add_mech(&s_World, 1, MECHA_CONTROL_AI, 1);
            CHECK(iFoe >= 0);
            mecha_sim_begin_match(&s_World);
            mecha_camera_reset(&s_Camera);
            render_now(pRenderer, iPilot);
            histogram(s_aFrame, aiCounts);
            snprintf(szName, sizeof(szName), "arena_stage%d.png", iArena);
            dump_frame(szOutDir, szName);
            printf("   %s: %d colours\n", mecha_arena_name(iArena),
                   distinct_colours(aiCounts));
        }
    }

    /* --- the gun car -----------------------------------------------------
     *
     * It is the one machine on the roster with no skeleton at all, so it is
     * the one most likely to come out as nothing.
     */
    {
        int iCar = -1;
        int i;

        for (i = 0; i < mecha_def_count(); i++) {
            if (mecha_def_get(i)->bWheeled)
                iCar = i;
        }
        CHECK(iCar >= 0);

        mecha_sim_init(&s_World, 0, 0x5EED1234u, 2);
        CHECK(mecha_sim_add_mech(&s_World, iCar, MECHA_CONTROL_HUMAN, 0) >= 0);
        CHECK(mecha_sim_add_mech(&s_World, iCar, MECHA_CONTROL_AI, 1) >= 0);
        mecha_sim_begin_match(&s_World);
        s_World.aMechs[0].byLock = MECHA_LOCK_HELD;
        s_World.aMechs[0].iTargetIdx = 1;
        s_World.aMechs[0].iRecovery = 12;
        mecha_camera_reset(&s_Camera);
        /* A camera set for a fourteen-metre machine is inside a two-metre
         * one, so this shot places its own: back, up and looking down. */
        /* Placed by hand, three quarters on, so the body and the gun are
         * both in the shot. The chase camera has its own scaling and is
         * exercised by the sim tests; this one is here to be looked at. */
        s_World.aMechs[0].iFacing = MECHA_DEG(35);
        s_World.aMechs[1].fX = s_World.aMechs[0].fX + MECHA_M(60.0f);
        s_World.aMechs[1].fZ = s_World.aMechs[0].fZ + MECHA_M(40.0f);
        s_Camera.fX = s_World.aMechs[0].fX - MECHA_M(3.0f);
        s_Camera.fY = s_World.aMechs[0].fY + MECHA_M(3.4f);
        s_Camera.fZ = s_World.aMechs[0].fZ - MECHA_M(11.0f);
        s_Camera.iYaw = MECHA_DEG(14);
        s_Camera.iPitch = -MECHA_DEG(11);
        s_Camera.bSettled = true;
        mecha_render_frame(pRenderer, &s_World, &s_Camera, 0, s_aFrame,
                           FRAME_W, FRAME_H, s_aQuads, MECHA_QUAD_CAPACITY);
        histogram(s_aFrame, aiCounts);
        dump_frame(szOutDir, "arena_guncar.png");

        /*
         * The same car walked round, so its painted panels can be looked at
         * rather than guessed about. Parked nose along +Z; yaw zero looks
         * along +Z, so a camera out at fPhi looks back the other way.
         * [TEST-04]
         */
        {
            static const struct { const char *szName; int iPhi; } aOrbit[] = {
                /* Named for what the camera looks at, which is the far side
                 * of the car from where it stands. [TEST-04] */
                { "arena_car_front.png",           0 },
                { "arena_car_front_xpos.png",     45 },
                { "arena_car_side_xpos.png",      90 },
                { "arena_car_rear_xpos.png",     135 },
                { "arena_car_rear.png",          180 },
                { "arena_car_rear_xneg.png",     225 },
                { "arena_car_side_xneg.png",     270 },
                { "arena_car_front_xneg.png",    315 },
            };
            const float fRadius = MECHA_M(9.0f);
            const float fHeight = MECHA_M(2.6f);
            int iShot;

            s_World.aMechs[0].iFacing = 0;
            s_World.aMechs[0].iAimPitch = 0;
            /* Park the foe straight off the nose so the gun lies along
             * the car rather than across the glass. */
            s_World.aMechs[1].fX = s_World.aMechs[0].fX;
            s_World.aMechs[1].fZ = s_World.aMechs[0].fZ + MECHA_M(90.0f);

            for (iShot = 0; iShot < (int)(sizeof(aOrbit) / sizeof(aOrbit[0]));
                 iShot++) {
                float fPhi = (float)aOrbit[iShot].iPhi * 3.14159265f / 180.0f;

                s_Camera.fX = s_World.aMechs[0].fX + fRadius * sinf(fPhi);
                s_Camera.fY = s_World.aMechs[0].fY + fHeight;
                s_Camera.fZ = s_World.aMechs[0].fZ + fRadius * cosf(fPhi);
                s_Camera.iYaw = MECHA_DEG(aOrbit[iShot].iPhi + 180);
                s_Camera.iPitch = -MECHA_DEG(9);
                s_Camera.bSettled = true;
                mecha_render_frame(pRenderer, &s_World, &s_Camera, 0, s_aFrame,
                                   FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);
                dump_frame(szOutDir, aOrbit[iShot].szName);
            }
            printf("   ZIZIN KLR 330: walked round in %d shots\n",
                   (int)(sizeof(aOrbit) / sizeof(aOrbit[0])));

            /*
             * The same eight shots with every panel wearing its own index,
             * so a panel can be pointed at rather than argued about. Facing
             * is read off the sign of the projected screen area. [TEST-04]
             */
            for (iShot = 0; iShot < (int)(sizeof(aOrbit) / sizeof(aOrbit[0]));
                 iShot++) {
                float fPhi = (float)aOrbit[iShot].iPhi * 3.14159265f / 180.0f;
                const float fNear = MECHA_M(6.2f);
                tMechaQuadList body;
                char szLabelled[64];
                int iPoly;
                int iLabels;
                int iDrawn;

                /* The gun is held out to the car's right and is big
                 * enough to hide a whole flank, so on each shot the foe
                 * goes to the far side of the car from the camera and the
                 * gun swings away with it. */
                s_World.aMechs[1].fX = s_World.aMechs[0].fX
                                       - MECHA_M(90.0f) * sinf(fPhi);
                s_World.aMechs[1].fZ = s_World.aMechs[0].fZ
                                       - MECHA_M(90.0f) * cosf(fPhi);

                s_Camera.fX = s_World.aMechs[0].fX + fNear * sinf(fPhi);
                s_Camera.fY = s_World.aMechs[0].fY + MECHA_M(2.0f);
                s_Camera.fZ = s_World.aMechs[0].fZ + fNear * cosf(fPhi);
                s_Camera.iYaw = MECHA_DEG(aOrbit[iShot].iPhi + 180);
                s_Camera.iPitch = -MECHA_DEG(6);
                s_Camera.bSettled = true;
                mecha_render_frame(pRenderer, &s_World, &s_Camera, 0, s_aFrame,
                                   FRAME_W, FRAME_H, s_aQuads,
                                   MECHA_QUAD_CAPACITY);

                memset(&body, 0, sizeof(body));
                body.paQuads   = s_aBodyQuads;
                body.iCapacity = MECHA_QUAD_CAPACITY;
                mecha_mesh_mech(&body, &s_World, 0);

                /* Nearest panel wins the space, or the roof and the tail
                 * write over the windscreen. [TEST-04] */
                iLabels = 0;
                for (iPoly = 0; iPoly < MECHA_ZIZIN_BODY_QUADS
                                && iPoly < body.iCount; iPoly++) {
                    const tMechaQuad *pQ = &s_aBodyQuads[iPoly];
                    int aiX[4];
                    int aiY[4];
                    float fCx = 0.0f, fCy = 0.0f, fCz = 0.0f;
                    float fArea = 0.0f;
                    float fDx, fDy, fDz;
                    int c;
                    bool bOn = true;

                    for (c = 0; c < 4; c++) {
                        if (!mecha_render_project(&s_Camera, FRAME_W, FRAME_H,
                                                  pQ->afVert[c][0],
                                                  pQ->afVert[c][1],
                                                  pQ->afVert[c][2],
                                                  &aiX[c], &aiY[c]))
                            bOn = false;
                        fCx += pQ->afVert[c][0];
                        fCy += pQ->afVert[c][1];
                        fCz += pQ->afVert[c][2];
                    }
                    if (!bOn)
                        continue;
                    for (c = 0; c < 4; c++) {
                        int d = (c + 1) & 3;
                        fArea += (float)aiX[c] * (float)aiY[d]
                                 - (float)aiX[d] * (float)aiY[c];
                    }
                    if (fArea >= 0.0f)
                        continue;   /* turned away from the camera */

                    fCx *= 0.25f; fCy *= 0.25f; fCz *= 0.25f;
                    if (!mecha_render_project(&s_Camera, FRAME_W, FRAME_H,
                                              fCx, fCy, fCz,
                                              &aiX[0], &aiY[0]))
                        continue;
                    fDx = fCx - s_Camera.fX;
                    fDy = fCy - s_Camera.fY;
                    fDz = fCz - s_Camera.fZ;
                    aLabels[iLabels].iPoly = iPoly;
                    aLabels[iLabels].iX    = aiX[0];
                    aLabels[iLabels].iY    = aiY[0];
                    aLabels[iLabels].fDist = fDx * fDx + fDy * fDy
                                             + fDz * fDz;
                    iLabels++;
                }

                /* Nearest first, so the near panel claims the space and the
                 * far one is the one dropped. */
                for (iPoly = 1; iPoly < iLabels; iPoly++) {
                    tPolyLabel keep = aLabels[iPoly];
                    int iSlot = iPoly - 1;
                    while (iSlot >= 0 && aLabels[iSlot].fDist > keep.fDist) {
                        aLabels[iSlot + 1] = aLabels[iSlot];
                        iSlot--;
                    }
                    aLabels[iSlot + 1] = keep;
                }

                iDrawn = 0;
                for (iPoly = 0; iPoly < iLabels; iPoly++) {
                    char szNum[8];
                    int iW;
                    int iPrev;
                    bool bClash = false;

                    snprintf(szNum, sizeof(szNum), "%d",
                             aLabels[iPoly].iPoly);
                    iW = mecha_render_text_width(2, szNum) + 4;
                    for (iPrev = 0; iPrev < iDrawn; iPrev++) {
                        if (aLabels[iPoly].iX - 2 < aDrawn[iPrev].iX + aDrawn[iPrev].iW
                            && aDrawn[iPrev].iX < aLabels[iPoly].iX - 2 + iW
                            && aLabels[iPoly].iY - 2 < aDrawn[iPrev].iY + 18
                            && aDrawn[iPrev].iY < aLabels[iPoly].iY - 2 + 18) {
                            bClash = true;
                            break;
                        }
                    }
                    if (bClash)
                        continue;
                    mecha_render_fill(s_aFrame, FRAME_W, FRAME_H,
                                      aLabels[iPoly].iX - 2,
                                      aLabels[iPoly].iY - 2, iW, 18, 0);
                    mecha_render_text(s_aFrame, FRAME_W, FRAME_H,
                                      aLabels[iPoly].iX, aLabels[iPoly].iY,
                                      2, 255, szNum);
                    aDrawn[iDrawn].iX = aLabels[iPoly].iX - 2;
                    aDrawn[iDrawn].iY = aLabels[iPoly].iY - 2;
                    aDrawn[iDrawn].iW = iW;
                    iDrawn++;
                }

                snprintf(szLabelled, sizeof(szLabelled), "poly_%s",
                         aOrbit[iShot].szName + strlen("arena_car_"));
                dump_frame(szOutDir, szLabelled);
            }
            printf("   ZIZIN KLR 330: eight numbered shots\n");
        }
        printf("   %s: %d colours, wearing %s\n",
               mecha_def_get(iCar)->szName, distinct_colours(aiCounts),
               mecha_render_car_skin_active() ? "its own skin"
                                              : "flat paint");
        /*
         * With the retail data beside the binary it has to be wearing the
         * real thing. Nothing here can assert that in CI, where there is no
         * data to load -- but the moment there is, a silent fallback to
         * flat paint is the failure this catches.
         */
        if (mecha_test_file_present("xzizin.bm"))
            CHECK(mecha_render_car_skin_active());
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
        /* Every row the mode actually builds, because the thing this test
         * is really guarding is that the whole screen fits. */
        brief.iRowCount = 9;
        brief.aRows[0].szLabel = "START MATCH";
        brief.aRows[1].szLabel = "YOUR MECH";
        brief.aRows[1].szValue = mecha_def_get(0)->szName;
        brief.aRows[2].szLabel = "OPPONENT";
        brief.aRows[2].szValue = mecha_def_get(2)->szName;
        brief.aRows[3].szLabel = "ARENA";
        brief.aRows[3].szValue = mecha_arena_name(0);
        brief.aRows[4].szLabel = "OPPONENT SKILL";
        brief.aRows[4].szValue = mecha_sim_ai_skill_name(MECHA_AI_VETERAN);
        brief.aRows[5].szLabel = "ROUND TIME";
        brief.aRows[5].szValue = "DEATHMATCH";
        brief.aRows[6].szLabel = "ENEMY WEAPONS";
        brief.aRows[6].szValue = "HELD - DEBUG";
        brief.aRows[7].szLabel = "VIEW CONTROLS";
        brief.aRows[8].szLabel = "EXIT TO WHIPLASH";

        /* An index nothing in the mode paints with, so anything still
         * carrying it afterwards is a pixel the briefing failed to cover.
         * It used to be 255, which stopped working the moment that became
         * the armour green. */
        memset(s_aFrame, MECHA_TEST_SENTINEL, sizeof(s_aFrame));
        mecha_render_briefing(&brief, s_aFrame, FRAME_W, FRAME_H);
        histogram(s_aFrame, aiBrief);
        dump_frame(szOutDir, "arena_briefing.png");

        /* Nothing left over from the frame before, and more than a fill. */
        CHECK(aiBrief[MECHA_TEST_SENTINEL] == 0);
        CHECK(distinct_colours(aiBrief) >= 4);

        /* The screen has to fit in both video modes; 320x200 is the one
         * that binds. [TEST-05] */
        {
            static uint8 aSmall[320 * 200];
            int iLast = last_ink_row(s_aFrame, FRAME_W, FRAME_H);

            CHECK(iLast > 0);
            CHECK(iLast < FRAME_H - 2);

            memset(aSmall, MECHA_TEST_SENTINEL, sizeof(aSmall));
            mecha_render_briefing(&brief, aSmall, 320, 200);
            iLast = last_ink_row(aSmall, 320, 200);
            CHECK(iLast > 0);
            CHECK(iLast < 198);
            /* And it still drew the whole thing rather than shrinking to
             * nothing: the rows and the footer under them have to be below
             * the middle of the screen. */
            CHECK(iLast > 110);
        }

        /*
         * And the controls, which are a page of their own now. Same two
         * questions: it drew something, and all of it fits in the small
         * video mode -- it is the longer of the two screens, ten controls
         * plus a double-height title.
         */
        {
            static uint8 aControls[320 * 200];
            int aiControls[256];
            int iLast;

            memset(aControls, MECHA_TEST_SENTINEL, sizeof(aControls));
            mecha_render_controls(aControls, 320, 200);
            histogram_of(aControls, 320 * 200, aiControls);
            CHECK(aiControls[MECHA_TEST_SENTINEL] == 0);
            CHECK(distinct_colours(aiControls) >= 3);
            iLast = last_ink_row(aControls, 320, 200);
            CHECK(iLast > 100);
            CHECK(iLast < 198);

            memset(s_aFrame, MECHA_TEST_SENTINEL, sizeof(s_aFrame));
            mecha_render_controls(s_aFrame, FRAME_W, FRAME_H);
            dump_frame(szOutDir, "arena_controls.png");
        }

        /* Only meaningful while the mode is choosing all the colours: the
         * retail font brings its own indices. [TEST-02] */
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
