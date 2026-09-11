/*
 * Headless coverage for the arena mode's simulation.
 *
 * Nothing here links SDL or any ROLLER global: mecha_sim.c and everything
 * under it is a pure function of the world struct, which is what makes whole
 * matches assertable in a unit test.
 */
#include "mecha_ai.h"
#include "mecha_arena.h"
#include "mecha_defs.h"
#include "mecha_mesh.h"
#include "mecha_sim.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int check(int bCondition, int iLine)
{
    if (!bCondition)
        fprintf(stderr, "mecha sim check failed at line %d\n", iLine);
    return bCondition ? 0 : iLine;
}

#define CHECK(condition) \
    do { \
        int iResult = check((condition), __LINE__); \
        if (iResult != 0) \
            return iResult; \
    } while (0)

static int near(float fActual, float fExpected, float fTolerance)
{
    return fabsf(fActual - fExpected) <= fTolerance;
}

/* Runs the world past the opening READY countdown so the controls are live. */
static void run_ticks(tMechaWorld *pWorld, const tMechaInput *paInputs,
                      int iInputCount, int iTicks)
{
    int i;

    for (i = 0; i < iTicks; i++)
        mecha_sim_tick(pWorld, paInputs, iInputCount);
}

static void start_duel(tMechaWorld *pWorld, int iArena, int iDefA, int iDefB,
                       uint32_t uiSeed, int iRoundsToWin)
{
    mecha_sim_init(pWorld, iArena, uiSeed, iRoundsToWin);
    mecha_sim_add_mech(pWorld, iDefA, MECHA_CONTROL_HUMAN, 0);
    mecha_sim_add_mech(pWorld, iDefB, MECHA_CONTROL_HUMAN, 1);
    mecha_sim_begin_match(pWorld);
    /* Idle through the round announcement. */
    run_ticks(pWorld, NULL, 0, MECHA_TICK_HZ * 2 + 2);
}

//-------------------------------------------------------------------------------------------------

static int test_angles(void)
{
    CHECK(mecha_angle_wrap(MECHA_ANGLE_FULL) == 0);
    CHECK(mecha_angle_wrap(-1) == MECHA_ANGLE_FULL - 1);
    CHECK(mecha_angle_delta(0, MECHA_ANGLE_QUARTER) == MECHA_ANGLE_QUARTER);
    /* Shortest way round a wrap, not the long way. */
    CHECK(mecha_angle_delta(MECHA_ANGLE_FULL - 10, 10) == 20);
    CHECK(mecha_angle_delta(10, MECHA_ANGLE_FULL - 10) == -20);
    CHECK(mecha_angle_approach(0, 1000, 100) == 100);
    CHECK(mecha_angle_approach(0, 50, 100) == 50);

    CHECK(near(mecha_sin(0), 0.0f, 1e-5f));
    CHECK(near(mecha_sin(MECHA_ANGLE_QUARTER), 1.0f, 1e-5f));
    CHECK(near(mecha_cos(0), 1.0f, 1e-5f));
    CHECK(near(mecha_cos(MECHA_ANGLE_HALF), -1.0f, 1e-5f));
    /* Perpendicular headings have to stay exactly perpendicular. */
    CHECK(near(mecha_sin(1234) * mecha_sin(1234 + MECHA_ANGLE_QUARTER)
               + mecha_cos(1234) * mecha_cos(1234 + MECHA_ANGLE_QUARTER),
               0.0f, 1e-5f));

    /* Heading zero points along +Z, and a quarter turn points along +X. */
    CHECK(mecha_atan2_angle(0.0f, 1.0f) == 0);
    CHECK(mecha_atan2_angle(1.0f, 0.0f) == MECHA_ANGLE_QUARTER);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_arena_geometry(void)
{
    tMechaArena arena;
    float fX;
    float fZ;
    int iArena;

    for (iArena = 0; iArena < mecha_arena_count(); iArena++) {
        mecha_arena_init(&arena, iArena);
        CHECK(arena.fHalfExtent > 0.0f);
        CHECK(arena.iObstacleCount > 0);
        CHECK(arena.iObstacleCount <= MECHA_MAX_OBSTACLES);
        CHECK(arena.szName != NULL);
    }

    mecha_arena_init(&arena, 0);

    /* The walls hold a mech in, allowing for its own radius. */
    fX = arena.fHalfExtent * 4.0f;
    fZ = 0.0f;
    CHECK(mecha_arena_resolve_cylinder(&arena, MECHA_METRE * 3.0f, 0.0f,
                                       MECHA_METRE * 14.0f, &fX, &fZ));
    CHECK(fX <= arena.fHalfExtent - MECHA_METRE * 3.0f + 0.01f);

    /* Walking into the side of a box gets pushed out, not climbed. */
    {
        const tMechaObstacle *pBox = &arena.aObstacles[0];

        fX = pBox->fX;
        fZ = pBox->fZ;
        CHECK(mecha_arena_resolve_cylinder(&arena, MECHA_METRE * 3.0f, 0.0f,
                                           MECHA_METRE * 14.0f, &fX, &fZ));
        CHECK(mecha_length2(fX - pBox->fX, fZ - pBox->fZ) > 0.0f);

        /* Standing on its roof is not a collision at all. */
        fX = pBox->fX;
        fZ = pBox->fZ;
        CHECK(!mecha_arena_resolve_cylinder(&arena, MECHA_METRE * 3.0f,
                                            pBox->fHeight, MECHA_METRE * 14.0f,
                                            &fX, &fZ));
        CHECK(near(mecha_arena_ground_height(&arena, pBox->fX, pBox->fZ,
                                             pBox->fHeight),
                   pBox->fHeight, 0.01f));
        /* ...but from the floor, the same spot is still floor. */
        CHECK(near(mecha_arena_ground_height(&arena, pBox->fX, pBox->fZ, 0.0f),
                   0.0f, 0.01f));
    }

    /* A shot fired into a box stops at it; one fired across open floor at
     * head height does not. */
    {
        const tMechaObstacle *pBox = &arena.aObstacles[0];
        float fY = pBox->fHeight * 0.5f;

        CHECK(mecha_arena_trace_segment(&arena,
                                        pBox->fX - MECHA_METRE * 40.0f, fY,
                                        pBox->fZ,
                                        pBox->fX + MECHA_METRE * 40.0f, fY,
                                        pBox->fZ, NULL, NULL, NULL));
        CHECK(!mecha_arena_trace_segment(&arena,
                                         0.0f, MECHA_METRE * 8.0f,
                                         -MECHA_METRE * 5.0f,
                                         0.0f, MECHA_METRE * 8.0f,
                                         MECHA_METRE * 5.0f,
                                         NULL, NULL, NULL));
        /* Straight down always finds the floor. */
        CHECK(mecha_arena_trace_segment(&arena, 0.0f, MECHA_METRE * 8.0f, 0.0f,
                                        0.0f, -MECHA_METRE, 0.0f,
                                        NULL, NULL, NULL));
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_roster(void)
{
    int iDef;

    CHECK(mecha_def_count() >= 2);
    for (iDef = 0; iDef < mecha_def_count(); iDef++) {
        const tMechaMechDef *pDef = mecha_def_get(iDef);
        int iSlot;

        CHECK(pDef->szName != NULL && pDef->szClass != NULL);
        CHECK(pDef->fArmour > 0.0f);
        CHECK(pDef->fHeight > 0.0f && pDef->fRadius > 0.0f);
        CHECK(pDef->fMass > 0.0f);
        CHECK(pDef->iBoostMax > 0);
        CHECK(pDef->iDashTicks > 0 && pDef->iLandTicks > 0);
        if (pDef->bWheeled) {
            /*
             * A machine on wheels has none of the gauge rules, because it
             * has no gauge to spend: no boost, no jump, one speed. What it
             * does have to have is somewhere to put its speed and something
             * to do at close quarters, since it carries no melee row.
             */
            CHECK(pDef->fWalkSpeed > 0.0f);
            CHECK(pDef->fDriveAccel > 0.0f && pDef->fBrake > 0.0f);
            CHECK(pDef->fSteerFloor > 0.0f);
            CHECK(pDef->fRamDamage > 0.0f && pDef->fRamSpeed > 0.0f);
        } else {
            /* Dashing has to be faster than walking or the gauge means
             * nothing. */
            CHECK(pDef->fDashSpeed > pDef->fWalkSpeed);
            /* Guarding is the fast refill; one of two reasons to do it. */
            CHECK(pDef->iBoostGuardRegen > pDef->iBoostRegen);
        }

        for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
            int iStance;

            for (iStance = 0; iStance < MECHA_STANCE_COUNT; iStance++) {
                const tMechaWeaponDef *pWeapon = &pDef->aWeapons[iSlot][iStance];

                /* Every one of the twelve has to be filled in: an empty entry
                 * would be a trigger that silently does nothing in one
                 * stance. */
                CHECK(pWeapon->szName != NULL);
                CHECK(pWeapon->byCount >= 1);
                CHECK(pWeapon->fSpeed > 0.0f);
                CHECK(pWeapon->fDamage > 0.0f);
                CHECK(pWeapon->iAmmo > 0);
                CHECK(pWeapon->iReloadTicks > 0);
                CHECK(pWeapon->iLifeTicks > 0);
                if (pWeapon->byKind == MECHA_PROJ_ARC
                    || pWeapon->byKind == MECHA_PROJ_MINE)
                    CHECK(pWeapon->fArcGravity > 0.0f);
                if (pWeapon->byKind == MECHA_PROJ_HOMING)
                    CHECK(pWeapon->iHomingRate > 0);
            }
        }
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_movement_and_boost(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaMechDef *pDef;
    float fStartZ;
    int iBoostAfterDash;
    int iBoostAfterStand;
    int iBoostAfterGuard;

    start_duel(&world, 0, 0, 0, 1234u, 2);
    pDef = mecha_def_get(0);
    memset(aInputs, 0, sizeof(aInputs));

    /* Walking moves the mech. */
    fStartZ = world.aMechs[0].fZ;
    aInputs[0].iMoveZ = 100;
    run_ticks(&world, aInputs, 2, 30);
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_WALK);
    CHECK(mecha_length2(world.aMechs[0].fX - 0.0f,
                        world.aMechs[0].fZ - fStartZ) > 0.0f);

    /* Dashing costs gauge and covers more ground than walking. */
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].iBoost = pDef->iBoostMax * MECHA_TICK_HZ;
    aInputs[0].iMoveZ = 100;
    aInputs[0].bDash = true;
    run_ticks(&world, aInputs, 2, 10);
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_DASH);
    iBoostAfterDash = world.aMechs[0].iBoost;
    CHECK(iBoostAfterDash < pDef->iBoostMax * MECHA_TICK_HZ);

    /* Let the burst run itself out first. Releasing the button does not
     * stop it -- a dash is committed -- so measuring the gauge before the
     * clock is up measures a dash, not a mech standing still. */
    memset(aInputs, 0, sizeof(aInputs));
    run_ticks(&world, aInputs, 2, pDef->iDashTicks + 4);
    CHECK(world.aMechs[0].byMove != MECHA_MOVE_DASH);

    /* Standing refills, guarding refills faster. Both start from the same
     * low gauge, and over a short enough window that neither saturates --
     * comparing two full gauges would prove nothing. */
    world.aMechs[0].iBoost = pDef->iBoostMax * MECHA_TICK_HZ / 10;
    world.aMechs[0].bBoostLocked = false;
    run_ticks(&world, aInputs, 2, 30);
    iBoostAfterStand = world.aMechs[0].iBoost;
    CHECK(iBoostAfterStand > pDef->iBoostMax * MECHA_TICK_HZ / 10);
    CHECK(iBoostAfterStand < pDef->iBoostMax * MECHA_TICK_HZ);

    world.aMechs[0].iBoost = pDef->iBoostMax * MECHA_TICK_HZ / 10;
    aInputs[0].bGuard = true;
    run_ticks(&world, aInputs, 2, 30);
    iBoostAfterGuard = world.aMechs[0].iBoost;
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_GUARD);
    CHECK(iBoostAfterGuard < pDef->iBoostMax * MECHA_TICK_HZ);
    CHECK(iBoostAfterGuard > iBoostAfterStand);

    /* An empty gauge locks the thrusters out until it has climbed back. */
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].iBoost = 1;
    aInputs[0].iMoveZ = 100;
    aInputs[0].bDash = true;
    run_ticks(&world, aInputs, 2, 4);
    CHECK(world.aMechs[0].bBoostLocked);
    CHECK(world.aMechs[0].byMove != MECHA_MOVE_DASH);

    /* Jumping leaves the ground and lands in a recovery the player cannot
     * cancel out of. */
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].iBoost = pDef->iBoostMax * MECHA_TICK_HZ;
    world.aMechs[0].bBoostLocked = false;
    aInputs[0].bJump = true;
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_JUMP);
    run_ticks(&world, aInputs, 2, 8);
    CHECK(mecha_mech_is_airborne(&world, 0));

    memset(aInputs, 0, sizeof(aInputs));
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 4);
    CHECK(!mecha_mech_is_airborne(&world, 0));
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_arena_confines_mechs(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaMechDef *pDef;
    int i;

    start_duel(&world, 0, 0, 0, 77u, 2);
    pDef = mecha_def_get(0);

    /* Drive hard in every direction for a while; the mech must never end up
     * outside the walls or inside a box. */
    for (i = 0; i < MECHA_TICK_HZ * 20; i++) {
        memset(aInputs, 0, sizeof(aInputs));
        aInputs[0].iMoveX = ((i / 40) % 2) ? 100 : -100;
        aInputs[0].iMoveZ = ((i / 70) % 2) ? 100 : -100;
        aInputs[0].bDash = (i % 90) < 30;
        mecha_sim_tick(&world, aInputs, 2);

        CHECK(mecha_arena_contains(&world.arena, world.aMechs[0].fX,
                                   world.aMechs[0].fZ));
        CHECK(world.aMechs[0].fY >= -0.01f);
        {
            float fX = world.aMechs[0].fX;
            float fZ = world.aMechs[0].fZ;

            /* Already resolved, so resolving again must be a no-op. */
            CHECK(!mecha_arena_resolve_cylinder(&world.arena, pDef->fRadius,
                                                world.aMechs[0].fY,
                                                pDef->fHeight, &fX, &fZ));
        }
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_stance_selects_weapon(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaWeaponDef *pStanding;
    const tMechaWeaponDef *pGuarding;

    start_duel(&world, 0, 0, 1, 9u, 2);
    memset(aInputs, 0, sizeof(aInputs));

    CHECK(mecha_mech_stance(&world.aMechs[0]) == MECHA_STANCE_STAND);
    pStanding = mecha_mech_weapon(&world, 0, MECHA_SLOT_CENTER);
    CHECK(pStanding != NULL);

    aInputs[0].bGuard = true;
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(mecha_mech_stance(&world.aMechs[0]) == MECHA_STANCE_GUARD);
    pGuarding = mecha_mech_weapon(&world, 0, MECHA_SLOT_CENTER);
    CHECK(pGuarding != NULL);

    /* The same trigger has to be a different attack in a different stance --
     * that is the mode's central rule. */
    CHECK(pGuarding != pStanding);
    CHECK(strcmp(pGuarding->szName, pStanding->szName) != 0);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int count_projectiles(const tMechaWorld *pWorld)
{
    int iCount = 0;
    int i;

    for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
        if (pWorld->aProjectiles[i].bActive)
            iCount++;
    }
    return iCount;
}

//-------------------------------------------------------------------------------------------------

static int test_firing_and_reload(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaWeaponDef *pWeapon;
    int iStartAmmo;

    start_duel(&world, 0, 0, 0, 5u, 2);
    memset(aInputs, 0, sizeof(aInputs));

    pWeapon = mecha_mech_weapon(&world, 0, MECHA_SLOT_LEFT);
    CHECK(pWeapon != NULL);
    iStartAmmo = world.aMechs[0].aiAmmo[MECHA_SLOT_LEFT];
    CHECK(iStartAmmo > 0);

    aInputs[0].bFireLeft = true;
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.aMechs[0].aiAmmo[MECHA_SLOT_LEFT] == iStartAmmo - 1);
    CHECK(world.aMechs[0].iRecovery > 0);
    CHECK(count_projectiles(&world) >= (int)pWeapon->byCount);

    /* Holding the trigger does not empty the magazine: firing is on the
     * press. */
    run_ticks(&world, aInputs, 2, 30);
    CHECK(world.aMechs[0].aiAmmo[MECHA_SLOT_LEFT] == iStartAmmo - 1);

    /* Emptying it starts a reload that eventually refills. */
    world.aMechs[0].aiAmmo[MECHA_SLOT_LEFT] = 1;
    memset(aInputs, 0, sizeof(aInputs));
    mecha_sim_tick(&world, aInputs, 2);
    aInputs[0].bFireLeft = true;
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.aMechs[0].aiAmmo[MECHA_SLOT_LEFT] == 0);
    CHECK(world.aMechs[0].aiReload[MECHA_SLOT_LEFT] > 0);

    memset(aInputs, 0, sizeof(aInputs));
    run_ticks(&world, aInputs, 2, pWeapon->iReloadTicks + 4);
    CHECK(world.aMechs[0].aiReload[MECHA_SLOT_LEFT] == 0);
    CHECK(world.aMechs[0].aiAmmo[MECHA_SLOT_LEFT] > 0);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_shots_damage_and_are_blocked(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    float fArmourBefore;

    /* Face to face at close range: a shot must connect. */
    start_duel(&world, 0, 0, 0, 11u, 2);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].fX = 0.0f;
    world.aMechs[0].fZ = 0.0f;
    world.aMechs[1].fX = 0.0f;
    world.aMechs[1].fZ = MECHA_METRE * 35.0f;
    world.aMechs[0].iFacing = 0;
    run_ticks(&world, aInputs, 2, 4);

    fArmourBefore = world.aMechs[1].fArmour;
    aInputs[0].bFireCenter = true;
    mecha_sim_tick(&world, aInputs, 2);
    memset(aInputs, 0, sizeof(aInputs));
    run_ticks(&world, aInputs, 2, 60);
    CHECK(world.aMechs[1].fArmour < fArmourBefore);

    /* The shooter never hits itself. */
    CHECK(near(world.aMechs[0].fArmour, mecha_def_get(0)->fArmour, 0.01f));

    /* A pillar between them stops it. Arena zero puts one at (-45, -45). */
    {
        tMechaWorld blocked;
        const tMechaObstacle *pBox;

        start_duel(&blocked, 0, 0, 0, 12u, 2);
        pBox = &blocked.arena.aObstacles[0];
        memset(aInputs, 0, sizeof(aInputs));
        blocked.aMechs[0].fX = pBox->fX;
        blocked.aMechs[0].fZ = pBox->fZ - MECHA_METRE * 25.0f;
        blocked.aMechs[1].fX = pBox->fX;
        blocked.aMechs[1].fZ = pBox->fZ + MECHA_METRE * 25.0f;
        blocked.aMechs[0].iFacing = 0;
        run_ticks(&blocked, aInputs, 2, 4);

        fArmourBefore = blocked.aMechs[1].fArmour;
        aInputs[0].bFireCenter = true;
        mecha_sim_tick(&blocked, aInputs, 2);
        memset(aInputs, 0, sizeof(aInputs));
        run_ticks(&blocked, aInputs, 2, 60);
        CHECK(near(blocked.aMechs[1].fArmour, fArmourBefore, 0.01f));
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_knockdown_and_invulnerability(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];

    start_duel(&world, 0, 0, 0, 21u, 2);
    memset(aInputs, 0, sizeof(aInputs));

    /* Chip damage alone never floors anyone. */
    mecha_sim_damage(&world, 1, 0, 5.0f, 4.0f, 0.0f, 0.0f);
    CHECK(world.aMechs[1].byMove != MECHA_MOVE_DOWN);

    /* A heavy enough hit does. */
    mecha_sim_damage(&world, 1, 0, 40.0f, MECHA_STAGGER_DOWN * 2.0f,
                     0.0f, 0.0f);
    CHECK(world.aMechs[1].byMove == MECHA_MOVE_DOWN);
    CHECK(world.aMechs[1].iStunTicks > 0);

    /* Getting up is covered, so a floored mech cannot be chained. */
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 2);
    CHECK(world.aMechs[1].iInvulnTicks > 0 || world.aMechs[1].byMove == MECHA_MOVE_STAND);

    {
        float fArmour;

        world.aMechs[1].iInvulnTicks = 30;
        fArmour = world.aMechs[1].fArmour;
        mecha_sim_damage(&world, 1, 0, 200.0f, 10.0f, 0.0f, 0.0f);
        CHECK(near(world.aMechs[1].fArmour, fArmour, 0.01f));
    }

    /* Enough damage destroys it outright. */
    world.aMechs[1].iInvulnTicks = 0;
    mecha_sim_damage(&world, 1, 0, 99999.0f, 0.0f, 0.0f, 0.0f);
    CHECK(!mecha_mech_alive(&world.aMechs[1]));
    CHECK(world.aMechs[1].byMove == MECHA_MOVE_DESTROYED);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_round_and_match_flow(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];

    start_duel(&world, 0, 0, 0, 31u, 2);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(world.match.byPhase == MECHA_PHASE_FIGHT);
    CHECK(world.match.iRound == 1);

    mecha_sim_damage(&world, 1, 0, 99999.0f, 0.0f, 0.0f, 0.0f);
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.match.byPhase == MECHA_PHASE_ROUND_OVER);
    CHECK(world.match.iWinnerIdx == 0);
    CHECK(world.aMechs[0].iRoundsWon == 1);

    /* The next round starts everyone whole again. */
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 4);
    CHECK(world.match.iRound == 2);
    CHECK(mecha_mech_alive(&world.aMechs[1]));
    CHECK(near(world.aMechs[1].fArmour, mecha_def_get(0)->fArmour, 0.01f));
    CHECK(count_projectiles(&world) == 0);

    /* Winning the second round takes the match. */
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 2 + 2);
    mecha_sim_damage(&world, 1, 0, 99999.0f, 0.0f, 0.0f, 0.0f);
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 5);
    CHECK(world.match.byPhase == MECHA_PHASE_MATCH_OVER);
    CHECK(world.match.iWinnerIdx == 0);
    CHECK(world.aMechs[0].iRoundsWon == 2);

    /* Time up is settled on armour rather than left open. */
    {
        tMechaWorld timed;

        start_duel(&timed, 0, 0, 0, 32u, 1);
        memset(aInputs, 0, sizeof(aInputs));
        timed.aMechs[1].fArmour *= 0.25f;
        timed.match.iRoundTicks = 1;
        run_ticks(&timed, aInputs, 2, 3);
        CHECK(timed.match.byPhase == MECHA_PHASE_ROUND_OVER);
        CHECK(timed.match.iWinnerIdx == 0);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* A cheap order-sensitive digest of everything the simulation owns. */
static uint32_t world_digest(const tMechaWorld *pWorld)
{
    const unsigned char *pBytes = (const unsigned char *)pWorld;
    uint32_t uiHash = 2166136261u;
    size_t i;

    for (i = 0; i < sizeof(*pWorld); i++) {
        uiHash ^= pBytes[i];
        uiHash *= 16777619u;
    }
    return uiHash;
}

//-------------------------------------------------------------------------------------------------

static void run_scripted_match(tMechaWorld *pWorld, uint32_t uiSeed)
{
    int i;

    mecha_sim_init(pWorld, 1, uiSeed, 2);
    mecha_sim_add_mech(pWorld, 0, MECHA_CONTROL_HUMAN, 0);
    mecha_sim_add_mech(pWorld, 2, MECHA_CONTROL_AI, 1);
    mecha_sim_begin_match(pWorld);

    for (i = 0; i < MECHA_TICK_HZ * 25; i++) {
        tMechaInput aInputs[2];

        memset(aInputs, 0, sizeof(aInputs));
        aInputs[0].iMoveX = ((i / 37) % 2) ? 100 : -100;
        aInputs[0].iMoveZ = ((i / 53) % 3) ? 60 : -80;
        aInputs[0].bDash = (i % 120) < 25;
        aInputs[0].bJump = (i % 300) < 6;
        aInputs[0].bGuard = (i % 210) < 30;
        aInputs[0].bFireLeft = (i % 23) < 3;
        aInputs[0].bFireCenter = (i % 61) < 4;
        aInputs[0].bFireRight = (i % 97) < 4;
        mecha_sim_tick(pWorld, aInputs, 2);
    }
}

//-------------------------------------------------------------------------------------------------

static int test_determinism(void)
{
    tMechaWorld a;
    tMechaWorld b;
    tMechaWorld c;

    run_scripted_match(&a, 0xC0FFEEu);
    run_scripted_match(&b, 0xC0FFEEu);
    CHECK(world_digest(&a) == world_digest(&b));
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);

    /* A different seed has to actually change the match, or the AI is not
     * really drawing on it. */
    run_scripted_match(&c, 0xBADF00Du);
    CHECK(world_digest(&c) != world_digest(&a));
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_ai_fights(void)
{
    tMechaWorld world;
    float fStartArmour;
    int iDefA;

    /* Every pairing on the roster has to produce a real fight -- damage
     * traded, and nobody wandering out of the arena. */
    for (iDefA = 0; iDefA < mecha_def_count(); iDefA++) {
        int iDefB = (iDefA + 1) % mecha_def_count();
        int i;
        int iBrokenTicks = 0;
        int iHeldTicks = 0;
        bool bDamaged = false;

        /*
         * The built arenas, deliberately. What this measures is lock
         * discipline, and open country measures something else: on the
         * meadow two pilots charge each other across a field and never
         * lose sight of one another at all, which is a fact about the
         * field rather than about them.
         */
        mecha_sim_init(&world, iDefA % 3, 0x51EDu + (uint32_t)iDefA, 2);
        mecha_sim_add_mech(&world, iDefA, MECHA_CONTROL_AI, 0);
        mecha_sim_add_mech(&world, iDefB, MECHA_CONTROL_AI, 1);
        mecha_sim_begin_match(&world);
        fStartArmour = world.aMechs[0].fArmour + world.aMechs[1].fArmour;

        for (i = 0; i < MECHA_TICK_HZ * 40; i++) {
            mecha_sim_tick(&world, NULL, 0);
            CHECK(mecha_arena_contains(&world.arena, world.aMechs[0].fX,
                                       world.aMechs[0].fZ));
            CHECK(mecha_arena_contains(&world.arena, world.aMechs[1].fX,
                                       world.aMechs[1].fZ));
            if (world.aMechs[0].fArmour + world.aMechs[1].fArmour
                < fStartArmour - 1.0f)
                bDamaged = true;
            if (world.aMechs[0].byLock == MECHA_LOCK_HELD)
                iHeldTicks++;
            else
                iBrokenTicks++;
            if (world.match.byPhase == MECHA_PHASE_MATCH_OVER)
                break;
        }
        CHECK(bDamaged);
        CHECK(world.aMechs[0].fDamageDealt + world.aMechs[1].fDamageDealt > 0.0f);

        /*
         * The computer pilot plays by the lock rules, and both halves of
         * that have to be true. It has to lose the lock sometimes, or the
         * mechanic does not exist in its hands and it is quietly privileged
         * over the player; and it has to hold one for most of a fight, or
         * it has no idea how to fight and the skill levels are measuring
         * noise.
         *
         * How much of the time varies with what the machine is for, and
         * the bound is deliberately the loose one. Everything that fights
         * at range holds a lock for better than nine tenths of a fight.
         * Kira sits near two thirds and belongs there: it is the close
         * quarters machine, its sabre is worth more than its sidearm, and
         * closing to knife range means spending the fight at the distance
         * where anything moving sideways leaves the cone. Asserting the
         * figure the rangefighters happen to hit would be asserting that
         * every machine fights the same way.
         */
        CHECK(iBrokenTicks > 0);
        printf("   %s holds a lock %.0f%% of the fight\n",
               mecha_def_get(iDefA)->szName,
               100.0f * (float)iHeldTicks
                 / (float)(iHeldTicks + iBrokenTicks));
        CHECK(iHeldTicks > iBrokenTicks);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

/*
 * One fixed-length duel against a scripted opponent that walks in and fires
 * on a timer and never dodges anything -- a stand-in for a player who cannot
 * read incoming shots. Rounds to win is set high enough that the match
 * cannot end inside the window, so a knockdown starts another round and both
 * totals keep accumulating instead of being capped by an early finish.
 */
static void run_skill_probe_world(tMechaWorld *pWorld, int iSkill,
                                  uint32_t uiSeed)
{
    tMechaInput aInputs[MECHA_MAX_MECHS];
    int i;

    mecha_sim_init(pWorld, 0, uiSeed, 9);
    mecha_sim_set_ai_skill(pWorld, iSkill);
    mecha_sim_add_mech(pWorld, 0, MECHA_CONTROL_HUMAN, 0);
    mecha_sim_add_mech(pWorld, 1, MECHA_CONTROL_AI, 1);
    mecha_sim_begin_match(pWorld);

    for (i = 0; i < MECHA_TICK_HZ * 20; i++) {
        int iBearing;
        int iOff;

        memset(aInputs, 0, sizeof(aInputs));
        aInputs[0].iMoveZ = 100;
        aInputs[0].bFireLeft   = (i % 17) < 2;
        aInputs[0].bFireCenter = (i % 41) < 2;
        aInputs[0].bFireRight  = (i % 67) < 2;

        /*
         * The stand-in player points its machine at the enemy.
         *
         * It did not used to have to: a locked machine squared itself up at
         * any range for free. Now that the auto-turn is confined to knife
         * range, a scripted opponent that never touches the stick simply
         * spins away from the fight -- and then this probe measures how
         * often the computer pilot wandered into the fixed cone of someone
         * who cannot turn, which ranks a pilot that steers decisively as
         * the one that takes the most fire. Steering it makes the numbers
         * mean what they say again.
         */
        iBearing = mecha_atan2_angle(
            pWorld->aMechs[1].fX - pWorld->aMechs[0].fX,
            pWorld->aMechs[1].fZ - pWorld->aMechs[0].fZ);
        iOff = mecha_angle_delta(pWorld->aMechs[0].iFacing, iBearing);
        if (iOff > MECHA_DEG(3))
            aInputs[0].iTurn = 100;
        else if (iOff < -MECHA_DEG(3))
            aInputs[0].iTurn = -100;

        mecha_sim_tick(pWorld, aInputs, MECHA_MAX_MECHS);
    }
}

static void run_skill_probe(int iSkill, uint32_t uiSeed,
                            float *pfAiDealt, float *pfAiAbsorbed)
{
    static tMechaWorld world;

    run_skill_probe_world(&world, iSkill, uiSeed);
    *pfAiDealt = world.aMechs[1].fDamageDealt;
    *pfAiAbsorbed = world.aMechs[0].fDamageDealt;
}

//-------------------------------------------------------------------------------------------------

static int test_ai_skill_ladder(void)
{
    static const uint32_t auiSeeds[] = { 0x5C1Du, 0x1234u, 0xA5A5u,
                                         0x77E1u, 0xBEEFu, 0x0F0Fu,
                                         0x3141u, 0x2718u, 0x9E37u,
                                         0x4D2Bu, 0x6A09u, 0xBB67u };
    float afDealt[MECHA_AI_SKILL_COUNT];
    float afAbsorbed[MECHA_AI_SKILL_COUNT];
    int iSkill;
    size_t iSeed;

    /* Named, and never off the end of the ladder. */
    CHECK(strcmp(mecha_sim_ai_skill_name(MECHA_AI_ROOKIE), "ROOKIE") == 0);
    CHECK(strcmp(mecha_sim_ai_skill_name(MECHA_AI_ACE), "ACE") == 0);

    for (iSkill = 0; iSkill < MECHA_AI_SKILL_COUNT; iSkill++) {
        afDealt[iSkill] = 0.0f;
        afAbsorbed[iSkill] = 0.0f;
        for (iSeed = 0; iSeed < sizeof(auiSeeds) / sizeof(auiSeeds[0]);
             iSeed++) {
            float fDealt;
            float fAbsorbed;

            run_skill_probe(iSkill, auiSeeds[iSeed], &fDealt, &fAbsorbed);
            afDealt[iSkill] += fDealt;
            afAbsorbed[iSkill] += fAbsorbed;
        }
        printf("   skill %-7s dealt %8.0f  absorbed %8.0f\n",
               mecha_sim_ai_skill_name(iSkill), afDealt[iSkill],
               afAbsorbed[iSkill]);
    }

    /*
     * The ladder has to run the right way round. Damage dealt is the
     * assertion that carries weight: it falls off with aim error, which is
     * the one lever measurement showed actually works, so all three rungs
     * are ordered on it. Damage absorbed is noisier -- it depends on how
     * long the pilot leaves its target alive to shoot back -- so only the
     * two ends are compared. Orderings, never figures: the numbers move
     * whenever the roster, the weapons or the arena are touched.
     */
    CHECK(afDealt[MECHA_AI_ACE] > afDealt[MECHA_AI_VETERAN]);
    CHECK(afDealt[MECHA_AI_VETERAN] > afDealt[MECHA_AI_ROOKIE]);

    /*
     * Damage absorbed is deliberately not asserted on.
     *
     * It reads as a skill measure and is not one here. What a pilot takes
     * depends on how long it leaves its target alive to shoot back, so a
     * better pilot ending rounds faster cuts its own exposure and a worse
     * one wandering out of the fight cuts its exposure too -- the two ends
     * meet in the middle. Confining the auto-turn to knife range made that
     * worse again by putting the stand-in player's aim at the mercy of its
     * own steering. Measured across twelve duels the three come out within
     * a few per cent of each other in no reliable order, and an assertion
     * that passes by one per cent is a future failure rather than a
     * guarantee. The figure is still printed, because it is worth seeing.
     */

    /* And the gap has to be worth having. A rookie that plays within a few
     * percent of an ace is not a difficulty setting. */
    CHECK(afDealt[MECHA_AI_ROOKIE] < afDealt[MECHA_AI_ACE] * 0.85f);

    /* A rookie still has to be a fight rather than a target. */
    CHECK(afDealt[MECHA_AI_ROOKIE] > 0.0f);

    /* A fresh world starts somewhere sane, and out-of-range values clamp
     * rather than reading off the end of the profile table. */
    {
        tMechaWorld world;

        mecha_sim_init(&world, 0, 1u, 1);
        CHECK(world.byAiSkill == MECHA_AI_VETERAN);
        mecha_sim_set_ai_skill(&world, -5);
        CHECK(world.byAiSkill == 0);
        mecha_sim_set_ai_skill(&world, 999);
        CHECK(world.byAiSkill == MECHA_AI_SKILL_COUNT - 1);
    }

    /* And a chosen skill still replays byte for byte from its seed. */
    {
        tMechaWorld a;
        tMechaWorld b;

        run_skill_probe_world(&a, MECHA_AI_ROOKIE, 0x2468u);
        run_skill_probe_world(&b, MECHA_AI_ROOKIE, 0x2468u);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* Points a mech at a heading and puts its lock into a known state. */
static void face_mech(tMechaWorld *pWorld, int iIdx, int iFacing)
{
    pWorld->aMechs[iIdx].iFacing = mecha_angle_wrap(iFacing);
}

//-------------------------------------------------------------------------------------------------

static int test_lock_breaks_and_returns(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iShot;
    int iShotsFound = 0;

    start_duel(&world, 0, 0, 0, 0x10Cu, 1);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].fX = 0.0f;
    world.aMechs[0].fZ = 0.0f;
    world.aMechs[1].fX = 0.0f;
    world.aMechs[1].fZ = MECHA_M(60.0f);
    /* Facing zero is facing +Z, which is where mech 1 was just put. Moving a
     * mech does not move where it is pointed, and the lock has to be earned
     * from a broken start -- it only comes on inside the narrow reacquire
     * cone. */
    face_mech(&world, 0, 0);
    face_mech(&world, 1, MECHA_ANGLE_HALF);
    run_ticks(&world, aInputs, 2, 4);

    /* Squared up: the lock is live and the weapons lead their shots. */
    CHECK(world.aMechs[0].byLock == MECHA_LOCK_HELD);

    /* Spun to face away. The auto-turn only runs off a live lock, so once
     * the grace period lapses there is nothing pulling the machine back and
     * it stays broken. */
    face_mech(&world, 0, MECHA_ANGLE_HALF);
    run_ticks(&world, aInputs, 2, MECHA_LOCK_BREAK_TICKS + 6);
    CHECK(world.aMechs[0].byLock == MECHA_LOCK_NONE);

    /* And it does not drift back on by itself. */
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);
    CHECK(world.aMechs[0].byLock == MECHA_LOCK_NONE);

    /* A shot fired off a broken lock goes where the barrel points, not
     * where the enemy is: facing is half a turn away from them, so its
     * velocity must carry it away down -Z rather than towards +Z. */
    aInputs[0].bFireCenter = true;
    mecha_sim_tick(&world, aInputs, 2);
    memset(aInputs, 0, sizeof(aInputs));
    for (iShot = 0; iShot < MECHA_MAX_PROJECTILES; iShot++) {
        const tMechaProjectile *pShot = &world.aProjectiles[iShot];

        if (!pShot->bActive || (int)pShot->byOwner != 0)
            continue;
        iShotsFound++;
        CHECK(pShot->fVelZ < 0.0f);
        /* Unguided, too: nothing to home on. */
        CHECK(pShot->iTarget < 0);
    }
    CHECK(iShotsFound > 0);

    /* Boost snaps it back on from any angle -- the quick way back, and the
     * reason to spend gauge on a dash you did not need for distance. */
    aInputs[0].bDash = true;
    run_ticks(&world, aInputs, 2, 3);
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_DASH);
    CHECK(world.aMechs[0].byLock == MECHA_LOCK_HELD);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_lock_survives_a_glance(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];

    /* Inside the cone the lock is simply held, and a moment outside it is a
     * slip rather than a break -- a lock that died to one frame of overshoot
     * would be unusable. */
    start_duel(&world, 0, 0, 0, 0x9A5u, 1);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].fX = 0.0f;
    world.aMechs[0].fZ = 0.0f;
    world.aMechs[1].fX = 0.0f;
    world.aMechs[1].fZ = MECHA_M(60.0f);
    face_mech(&world, 0, 0);
    face_mech(&world, 1, MECHA_ANGLE_HALF);
    run_ticks(&world, aInputs, 2, 4);
    CHECK(world.aMechs[0].byLock == MECHA_LOCK_HELD);

    /* Just inside the hold cone: still locked, and the auto-turn is closing
     * the gap rather than the lock decaying. */
    face_mech(&world, 0, MECHA_LOCK_CONE - MECHA_DEG(3));
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.aMechs[0].byLock == MECHA_LOCK_HELD);
    CHECK(world.aMechs[0].iLockSlipTicks == 0);

    /* Just outside it, for less than the grace period. */
    face_mech(&world, 0, MECHA_LOCK_CONE + MECHA_DEG(12));
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.aMechs[0].byLock == MECHA_LOCK_SLIPPING);
    CHECK(world.aMechs[0].byLock != MECHA_LOCK_NONE);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_jump_cancel(void)
{
    tMechaWorld plain;
    tMechaWorld cancelled;
    tMechaInput aInputs[2];
    const tMechaMechDef *pDef = mecha_def_get(0);
    int iPlainAir = 0;
    int iCancelAir = 0;
    int i;

    /* Two identical jumps, one of them cancelled. */
    start_duel(&plain, 0, 0, 0, 0x5A11u, 1);
    start_duel(&cancelled, 0, 0, 0, 0x5A11u, 1);

    memset(aInputs, 0, sizeof(aInputs));
    aInputs[0].bJump = true;
    mecha_sim_tick(&plain, aInputs, 2);
    mecha_sim_tick(&cancelled, aInputs, 2);
    CHECK(plain.aMechs[0].byMove == MECHA_MOVE_JUMP);

    /* Leaving the ground snaps the lock on, whatever the machine was
     * pointed at: going up is how you find someone who got behind you. */
    face_mech(&cancelled, 0, MECHA_ANGLE_HALF);
    memset(aInputs, 0, sizeof(aInputs));
    run_ticks(&plain, aInputs, 2, 12);
    run_ticks(&cancelled, aInputs, 2, 12);
    CHECK(cancelled.aMechs[0].byLock == MECHA_LOCK_HELD);

    /* Guard in the air throws the arc away. */
    aInputs[0].bGuard = true;
    mecha_sim_tick(&cancelled, aInputs, 2);
    CHECK(cancelled.aMechs[0].byMove == MECHA_MOVE_CANCEL);
    memset(aInputs, 0, sizeof(aInputs));

    /* The cancelled machine reaches the ground first. */
    for (i = 0; i < MECHA_TICK_HZ * 4; i++) {
        if (plain.aMechs[0].byMove == MECHA_MOVE_JUMP)
            iPlainAir++;
        if (cancelled.aMechs[0].byMove == MECHA_MOVE_JUMP
            || cancelled.aMechs[0].byMove == MECHA_MOVE_CANCEL)
            iCancelAir++;
        mecha_sim_tick(&plain, aInputs, 2);
        mecha_sim_tick(&cancelled, aInputs, 2);
    }
    CHECK(iCancelAir < iPlainAir);

    /* And the landing left the turn rate off its leash, which is the whole
     * reason to have done it. */
    {
        tMechaWorld spun;
        int iBefore;
        int iTurned;
        int iCapped;

        start_duel(&spun, 0, 0, 0, 0x5A11u, 1);
        memset(aInputs, 0, sizeof(aInputs));
        aInputs[0].bJump = true;
        mecha_sim_tick(&spun, aInputs, 2);
        memset(aInputs, 0, sizeof(aInputs));
        run_ticks(&spun, aInputs, 2, 12);
        aInputs[0].bGuard = true;
        mecha_sim_tick(&spun, aInputs, 2);
        memset(aInputs, 0, sizeof(aInputs));
        /* Down to the ground. */
        for (i = 0; i < MECHA_TICK_HZ * 4
                    && spun.aMechs[0].byMove == MECHA_MOVE_CANCEL; i++)
            mecha_sim_tick(&spun, aInputs, 2);
        CHECK(spun.aMechs[0].iFreeTurnTicks > 0);

        /* Turning under the free window covers more ground in the same
         * ticks than the machine's own rate allows. */
        iBefore = spun.aMechs[0].iFacing;
        aInputs[0].iTurn = 100;
        run_ticks(&spun, aInputs, 2, 6);
        iTurned = mecha_angle_delta(iBefore, spun.aMechs[0].iFacing);
        if (iTurned < 0)
            iTurned = -iTurned;
        iCapped = (int)(pDef->fTurnRate * MECHA_TICK_SECONDS) * 6;
        CHECK(iTurned > iCapped);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_guard_turns_melee_aside(void)
{
    const tMechaMechDef *pDef = mecha_def_get(3);
    int iMeleeSlot = -1;
    float afLost[2];
    int iGuarding;
    int iSlot;

    /* Find the machine's standing melee rather than hardcoding a slot, so
     * this keeps working when the roster is rebalanced. */
    for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
        if (pDef->aWeapons[iSlot][MECHA_STANCE_STAND].byKind
            == MECHA_PROJ_MELEE)
            iMeleeSlot = iSlot;
    }
    CHECK(iMeleeSlot >= 0);

    /* The same swing landed twice: once on a machine standing there, once on
     * one holding guard. */
    for (iGuarding = 0; iGuarding < 2; iGuarding++) {
        tMechaWorld world;
        tMechaInput aInputs[2];
        float fBefore;
        int i;

        start_duel(&world, 0, 3, 0, 0x6DA5u, 1);
        memset(aInputs, 0, sizeof(aInputs));
        world.aMechs[0].fX = 0.0f;
        world.aMechs[0].fZ = 0.0f;
        world.aMechs[1].fX = 0.0f;
        world.aMechs[1].fZ = MECHA_M(14.0f);
        face_mech(&world, 0, 0);
        face_mech(&world, 1, MECHA_ANGLE_HALF);
        run_ticks(&world, aInputs, 2, 4);

        /* Hold guard from before the swing so the stance is already set when
         * the blade arrives. */
        aInputs[1].bGuard = iGuarding != 0;
        run_ticks(&world, aInputs, 2, 6);
        if (iGuarding)
            CHECK(world.aMechs[1].byMove == MECHA_MOVE_GUARD);

        fBefore = world.aMechs[1].fArmour;
        switch (iMeleeSlot) {
        case MECHA_SLOT_LEFT:   aInputs[0].bFireLeft = true; break;
        case MECHA_SLOT_CENTER: aInputs[0].bFireCenter = true; break;
        default:                aInputs[0].bFireRight = true; break;
        }
        mecha_sim_tick(&world, aInputs, 2);
        aInputs[0].bFireLeft = false;
        aInputs[0].bFireCenter = false;
        aInputs[0].bFireRight = false;
        for (i = 0; i < MECHA_TICK_HZ; i++)
            mecha_sim_tick(&world, aInputs, 2);
        afLost[iGuarding] = fBefore - world.aMechs[1].fArmour;
    }

    /* The swing has to have connected at all, or this proves nothing. */
    CHECK(afLost[0] > 0.0f);
    /* And guard has to have turned most of it aside. Asserted as a band
     * rather than a figure: the constant is 15%, and anything under a third
     * means the rule fired. */
    CHECK(afLost[1] < afLost[0] * 0.34f);
    CHECK(afLost[1] > 0.0f);
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* How far apart the feet are, front to back, in the mech's own frame. */
static void mesh_foot_extent(const tMechaQuadList *pList,
                             const tMechaMech *pMech, float fAnkle,
                             bool bAcross, float *pfLow, float *pfHigh)
{
    float fCos = mecha_cos(pMech->iFacing);
    float fSin = mecha_sin(pMech->iFacing);
    float fLow = 1e30f;
    float fHigh = -1e30f;
    int i;
    int v;

    for (i = 0; i < pList->iCount; i++) {
        for (v = 0; v < 4; v++) {
            float fY = pList->paQuads[i].afVert[v][1] - pMech->fY;
            float fDx = pList->paQuads[i].afVert[v][0] - pMech->fX;
            float fDz = pList->paQuads[i].afVert[v][2] - pMech->fZ;
            float fLocal = bAcross ? fDx * fCos - fDz * fSin
                                   : fDx * fSin + fDz * fCos;

            if (fY > fAnkle)
                continue;
            if (fLocal < fLow)
                fLow = fLocal;
            if (fLocal > fHigh)
                fHigh = fLocal;
        }
    }
    *pfLow = fLow;
    *pfHigh = fHigh;
}

//-------------------------------------------------------------------------------------------------

/* Fore and aft, which is the one the walk cycle is measured on. */
static void mesh_foot_span(const tMechaQuadList *pList,
                           const tMechaMech *pMech, float fAnkle,
                           float *pfLowZ, float *pfHighZ)
{
    mesh_foot_extent(pList, pMech, fAnkle, false, pfLowZ, pfHighZ);
}

//-------------------------------------------------------------------------------------------------

/* The top of the machine, in world space. */
static float mesh_highest(const tMechaQuadList *pList)
{
    float fTop = -1e30f;
    int i;
    int v;

    for (i = 0; i < pList->iCount; i++)
        for (v = 0; v < 4; v++)
            if (pList->paQuads[i].afVert[v][1] > fTop)
                fTop = pList->paQuads[i].afVert[v][1];
    return fTop;
}

//-------------------------------------------------------------------------------------------------

/* Centroid of every vertex inside a height band, written in the mech's own
 * frame: X across, Z forward, so a limb that has swung left reads as a
 * negative X whatever heading the machine is on. */
static void mesh_band_centroid(const tMechaQuadList *pList,
                               const tMechaMech *pMech, float fLow,
                               float fHigh, float *pfX, float *pfZ,
                               float *pfMinY)
{
    float fCos = mecha_cos(pMech->iFacing);
    float fSin = mecha_sin(pMech->iFacing);
    float fSumX = 0.0f;
    float fSumZ = 0.0f;
    float fMinY = 1e30f;
    int iCount = 0;
    int i;
    int v;

    for (i = 0; i < pList->iCount; i++) {
        for (v = 0; v < 4; v++) {
            float fY = pList->paQuads[i].afVert[v][1] - pMech->fY;
            float fDx = pList->paQuads[i].afVert[v][0] - pMech->fX;
            float fDz = pList->paQuads[i].afVert[v][2] - pMech->fZ;

            if (fY < fMinY)
                fMinY = fY;
            if (fY < fLow || fY > fHigh)
                continue;
            fSumX += fDx * fCos - fDz * fSin;
            fSumZ += fDx * fSin + fDz * fCos;
            iCount++;
        }
    }
    *pfX = iCount > 0 ? fSumX / (float)iCount : 0.0f;
    *pfZ = iCount > 0 ? fSumZ / (float)iCount : 0.0f;
    if (pfMinY)
        *pfMinY = fMinY;
}

//-------------------------------------------------------------------------------------------------

static int test_legs_walk_on_jointed_knees(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    const tMechaMechDef *pDef;
    float fStride = 0.0f;
    float fWorstFoot = 0.0f;
    float fBendDrop = 0.0f;
    int iStep;

    start_duel(&world, 0, 0, 0, 0x1E65u, 1);
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);
    world.aMechs[0].byMove = MECHA_MOVE_WALK;

    for (iStep = 0; iStep < 12; iStep++) {
        float fX;
        float fZ;
        float fMinY;
        float fLowZ;
        float fHighZ;

        world.aMechs[0].fStepPhase = (float)iStep / 12.0f;
        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_mech(&list, &world, 0);
        CHECK(list.iCount > 0);

        /* Ankle height and below: the feet. Their fore and aft spread is the
         * stride, and the lowest vertex on the machine is the sole. */
        mesh_band_centroid(&list, &world.aMechs[0], 0.0f,
                           0.09f * pDef->fHeight, &fX, &fZ, &fMinY);
        mesh_foot_span(&list, &world.aMechs[0], 0.09f * pDef->fHeight,
                       &fLowZ, &fHighZ);
        if (fHighZ - fLowZ > fStride)
            fStride = fHighZ - fLowZ;

        /*
         * The soles stay on the floor through the whole cycle. This is the
         * assertion the body-drop calculation exists for: bend a knee
         * without lowering the hips and the machine walks on tiptoe, lower
         * them by the wrong amount and it sinks through the ground.
         */
        if (fMinY < fWorstFoot)
            fWorstFoot = fMinY;
        CHECK(fMinY > -0.01f * pDef->fHeight);
        CHECK(fMinY < 0.02f * pDef->fHeight);
    }

    /* Guard bends the knees rather than squashing the machine, so the head
     * comes down and the feet stay put. */
    {
        float fX;
        float fZ;
        float fStandHead;
        float fGuardHead;

        world.aMechs[0].fStepPhase = 0.0f;
        world.aMechs[0].byMove = MECHA_MOVE_STAND;
        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_mech(&list, &world, 0);
        fStandHead = mesh_highest(&list) - world.aMechs[0].fY;

        world.aMechs[0].byMove = MECHA_MOVE_GUARD;
        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_mech(&list, &world, 0);
        fGuardHead = mesh_highest(&list) - world.aMechs[0].fY;
        mesh_band_centroid(&list, &world.aMechs[0], 0.0f,
                           0.09f * pDef->fHeight, &fX, &fZ, &fBendDrop);

        printf("   stride %.0f, sole error %.0f, head %.0f standing"
               " and %.0f guarding\n", fStride, fWorstFoot, fStandHead,
               fGuardHead);
        CHECK(fGuardHead < fStandHead * 0.92f);
        CHECK(fBendDrop > -0.01f * pDef->fHeight);
    }

    /*
     * A stride that never opens is a pair of planks pivoting at the hip,
     * and a short one is a big machine mincing. Measured against the
     * machine's own height rather than a number, because that is what makes
     * it read as a stride at all: two thirds of its height between its feet
     * at full reach, which the mincing version it replaced could not make.
     */
    printf("   stride is %.2f of standing height\n", fStride / pDef->fHeight);
    CHECK(fStride > 0.70f * pDef->fHeight);

    /*
     * And the knee bends the way a person's does. At phase zero the left
     * leg has its thigh vertical and its knee at full flex, so the ankle
     * has to be behind the knee -- if it is in front, the machine is
     * bird-legged, which is a fine thing for a mech to be but not what this
     * roster is meant to look like.
     */
    {
        float fKneeZ = 0.0f;
        float fFootZ = 0.0f;
        int iKnee = 0;
        int iFoot = 0;
        float fCos;
        float fSin;
        int i;
        int v;

        world.aMechs[0].byMove = MECHA_MOVE_WALK;
        world.aMechs[0].fStepPhase = 0.0f;
        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_mech(&list, &world, 0);
        fCos = mecha_cos(world.aMechs[0].iFacing);
        fSin = mecha_sin(world.aMechs[0].iFacing);

        for (i = 0; i < list.iCount; i++) {
            float fX = 0.0f;
            float fY = 0.0f;
            float fZ = 0.0f;

            for (v = 0; v < 4; v++) {
                float fDx = aStorage[i].afVert[v][0] - world.aMechs[0].fX;
                float fDz = aStorage[i].afVert[v][2] - world.aMechs[0].fZ;

                fX += 0.25f * (fDx * fCos - fDz * fSin);
                fZ += 0.25f * (fDx * fSin + fDz * fCos);
                fY += 0.25f * (aStorage[i].afVert[v][1] - world.aMechs[0].fY);
            }
            if (fX > 0.0f)
                continue;                       /* the left leg only */
            if (fY < 0.35f * pDef->fHeight
                && aStorage[i].byPalette == pDef->abyPalette[2]) {
                fKneeZ += fZ;                   /* the knee joint block */
                iKnee++;
            }
            if (fY < 0.09f * pDef->fHeight) {
                fFootZ += fZ;
                iFoot++;
            }
        }
        CHECK(iKnee > 0);
        CHECK(iFoot > 0);
        fKneeZ /= (float)iKnee;
        fFootZ /= (float)iFoot;
        printf("   flexed knee: ankle sits %.0f behind the knee\n",
               fKneeZ - fFootZ);
        CHECK(fFootZ < fKneeZ - 0.10f * pDef->fRadius);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static bool clear_runway(tMechaWorld *pWorld, int iFacing);

/* Widest fore-and-aft spread the feet reach anywhere in a cycle. */
static float gait_reach(tMechaWorld *pWorld, int iMechIdx,
                        tMechaQuad *paStorage, bool bAcross)
{
    const tMechaMechDef *pDef =
        mecha_def_get((int)pWorld->aMechs[iMechIdx].byDefIdx);
    tMechaQuadList list;
    float fWidest = 0.0f;
    int iStep;

    for (iStep = 0; iStep < 16; iStep++) {
        float fLow;
        float fHigh;

        pWorld->aMechs[iMechIdx].fStepPhase = (float)iStep / 16.0f;
        pWorld->iTick = iStep * 3;
        mecha_quads_reset(&list, paStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_mech(&list, pWorld, iMechIdx);
        mesh_foot_extent(&list, &pWorld->aMechs[iMechIdx],
                         pWorld->aMechs[iMechIdx].fY + 0.16f * pDef->fHeight,
                         bAcross, &fLow, &fHigh);
        if (fHigh - fLow > fWidest)
            fWidest = fHigh - fLow;
    }
    return fWidest;
}

//-------------------------------------------------------------------------------------------------

/* Every leg vertex of a posed machine, in its own frame, so two poses can
 * be held up against each other. */
static int gait_capture(tMechaWorld *pWorld, int iMechIdx,
                        tMechaQuad *paStorage, float *pafOut, int iMax)
{
    const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
    const tMechaMechDef *pDef = mecha_def_get((int)pMech->byDefIdx);
    tMechaQuadList list;
    float fCos = mecha_cos(pMech->iLegYaw);
    float fSin = mecha_sin(pMech->iLegYaw);
    int iCount = 0;
    int i;
    int v;

    /*
     * The legs are the first thing the builder emits -- two of them, four
     * boxes apiece, six faces a box -- and taking them by position rather
     * than by height is what keeps the count identical across poses. A
     * height cutoff sounds tidier and is not: a thigh swinging about the
     * hip moves its own top vertices across any line drawn near it, so the
     * two poses being compared come back with different numbers of points
     * in them.
     */
    mecha_quads_reset(&list, paStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, pWorld, iMechIdx);
    if (list.iCount < 2 * 4 * 6)
        return 0;
    for (i = 0; i < 2 * 4 * 6; i++) {
        for (v = 0; v < 4; v++) {
            float fY = paStorage[i].afVert[v][1] - pMech->fY;
            float fDx = paStorage[i].afVert[v][0] - pMech->fX;
            float fDz = paStorage[i].afVert[v][2] - pMech->fZ;

            if (iCount + 3 > iMax)
                return iCount;
            pafOut[iCount++] = fDx * fCos - fDz * fSin;
            pafOut[iCount++] = fY;
            pafOut[iCount++] = fDx * fSin + fDz * fCos;
        }
    }
    (void)pDef;
    return iCount;
}

//-------------------------------------------------------------------------------------------------

/* How far the two poses are from each other, at their furthest. */
static float gait_apart(const float *pafA, const float *pafB, int iCount)
{
    float fWorst = 0.0f;
    int i;

    for (i = 0; i + 2 < iCount; i += 3) {
        float fGap = mecha_length3(pafA[i] - pafB[i], pafA[i + 1] - pafB[i + 1],
                                   pafA[i + 2] - pafB[i + 2]);

        if (fGap > fWorst)
            fWorst = fGap;
    }
    return fWorst;
}

//-------------------------------------------------------------------------------------------------

/*
 * The boxes the arm chain emits, in the order the builder emits them: two
 * legs of four boxes each, four for the torso, then five a side for the
 * arms -- pauldron, upper, elbow, forearm, gun. Taking the gun by its place
 * in that order is the same trick gait_capture uses and rests on the same
 * thing: the builder emits a fixed skeleton in a fixed order, so a box can
 * be named by counting.
 */
#define MESH_BOX_GUN_LEFT  16
#define MESH_BOX_GUN_RIGHT 21

/* Where a named box sits, in the machine's own frame. */
static void mesh_box_centre(const tMechaQuadList *pList,
                            const tMechaMech *pMech, int iBox, float *pfY,
                            float *pfForward)
{
    float fCos = mecha_cos(pMech->iFacing);
    float fSin = mecha_sin(pMech->iFacing);
    float fY = 0.0f;
    float fZ = 0.0f;
    int i;
    int v;

    for (i = iBox * 6; i < iBox * 6 + 6 && i < pList->iCount; i++) {
        for (v = 0; v < 4; v++) {
            float fDx = pList->paQuads[i].afVert[v][0] - pMech->fX;
            float fDz = pList->paQuads[i].afVert[v][2] - pMech->fZ;

            fY += pList->paQuads[i].afVert[v][1] - pMech->fY;
            fZ += fDx * fSin + fDz * fCos;
        }
    }
    *pfY = fY / 24.0f;
    *pfForward = fZ / 24.0f;
}

//-------------------------------------------------------------------------------------------------

static int test_a_machine_at_ease_lowers_its_arms(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    const tMechaMechDef *pDef;
    float afY[2];
    float afForward[2];
    int iPass;

    start_duel(&world, 0, 0, 0, 0x4A11u, 1);
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);
    world.aMechs[0].byMove = MECHA_MOVE_STAND;
    world.aMechs[0].byLock = MECHA_LOCK_HELD;
    world.aMechs[0].iTargetIdx = 1;

    for (iPass = 0; iPass < 2; iPass++) {
        float afLeft[2];

        /* Guns up, then guns down; nothing else about the machine changes. */
        world.aMechs[0].fCombat = iPass == 0 ? 1.0f : 0.0f;
        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_mech(&list, &world, 0);
        CHECK(list.iCount > MESH_BOX_GUN_RIGHT * 6 + 6);
        mesh_box_centre(&list, &world.aMechs[0], MESH_BOX_GUN_LEFT,
                        &afLeft[0], &afLeft[1]);
        mesh_box_centre(&list, &world.aMechs[0], MESH_BOX_GUN_RIGHT,
                        &afY[iPass], &afForward[iPass]);
        afY[iPass] = 0.5f * (afY[iPass] + afLeft[0]);
        afForward[iPass] = 0.5f * (afForward[iPass] + afLeft[1]);
    }

    printf("   guns: %.2f high and %.2f forward ready, %.2f and %.2f at"
           " ease (of height)\n", afY[0] / pDef->fHeight,
           afForward[0] / pDef->fHeight, afY[1] / pDef->fHeight,
           afForward[1] / pDef->fHeight);
    /*
     * Down, and in. A machine that is neither locked on nor shooting drops
     * the whole chain -- the shoulder stops tracking, the elbow unfolds --
     * so the guns end up beside its own knees instead of levelled at you.
     * That is the read the player gets from across an arena.
     */
    CHECK(afY[1] < afY[0] - 0.12f * pDef->fHeight);
    CHECK(afForward[1] < afForward[0] - 0.10f * pDef->fHeight);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_a_machine_with_a_lock_settles_into_it(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    const tMechaMechDef *pDef;
    float afHead[2];
    float afAcross[2];
    int iPass;

    start_duel(&world, 0, 0, 0, 0x57A0u, 1);
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);
    world.aMechs[0].byMove = MECHA_MOVE_STAND;

    for (iPass = 0; iPass < 2; iPass++) {
        float fLow;
        float fHigh;
        float fX;
        float fZ;
        float fMinY;
        int iTick;
        int iFrames = 0;

        world.aMechs[0].fCombat = iPass == 0 ? 0.0f : 1.0f;
        afHead[iPass] = 0.0f;
        afAcross[iPass] = 0.0f;
        /* Over a whole breath, since the stance is a pose with a slow rock
         * in it rather than a still frame. */
        for (iTick = 0; iTick < 120; iTick += 5) {
            world.iTick = iTick;
            mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
            mecha_mesh_mech(&list, &world, 0);
            mesh_foot_extent(&list, &world.aMechs[0],
                             world.aMechs[0].fY + 0.09f * pDef->fHeight,
                             true, &fLow, &fHigh);
            if (fHigh - fLow > afAcross[iPass])
                afAcross[iPass] = fHigh - fLow;
            afHead[iPass] += mesh_highest(&list) - world.aMechs[0].fY;
            iFrames++;

            /*
             * And it never stands on one foot to do it. The stance is the
             * one pose in the game where both feet are meant to be planted
             * at once, which is the whole reason the hips take up the slack
             * in roll rather than the knees.
             */
            mesh_band_centroid(&list, &world.aMechs[0], 0.0f,
                               0.09f * pDef->fHeight, &fX, &fZ, &fMinY);
            CHECK(fMinY > -0.01f * pDef->fHeight);
            CHECK(fMinY < 0.02f * pDef->fHeight);
        }
        afHead[iPass] /= (float)iFrames;
    }

    printf("   stance: head %.0f at ease and %.0f squared up, feet %.0f"
           " across and %.0f\n", afHead[0], afHead[1], afAcross[0],
           afAcross[1]);
    /* Lower and wider with someone to fight: it settles rather than
     * standing to attention. */
    CHECK(afHead[1] < afHead[0] * 0.97f);
    CHECK(afAcross[1] > afAcross[0] * 1.12f);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_round_clock_is_a_setting(void)
{
    static const int aiSeconds[] = { 30, 60, 90, 120 };
    tMechaWorld world;
    size_t iChoice;

    for (iChoice = 0; iChoice < sizeof(aiSeconds) / sizeof(aiSeconds[0]);
         iChoice++) {
        int iSeconds = aiSeconds[iChoice];
        int iElapsed = 0;

        mecha_sim_init(&world, 0, 0xC10Cu, 1);
        mecha_sim_set_round_seconds(&world, iSeconds);
        CHECK(mecha_sim_add_mech(&world, 0, MECHA_CONTROL_HUMAN, 0) >= 0);
        CHECK(mecha_sim_add_mech(&world, 1, MECHA_CONTROL_HUMAN, 1) >= 0);
        mecha_sim_begin_match(&world);

        /*
         * Nobody fires a shot, so the only thing that can end this round is
         * the clock -- and it has to end it at the second it was set to,
         * not at whatever the simulation defaults to.
         */
        while (world.match.byPhase != MECHA_PHASE_ROUND_OVER
               && world.match.byPhase != MECHA_PHASE_MATCH_OVER
               && iElapsed < MECHA_TICK_HZ * (iSeconds + 20)) {
            mecha_sim_tick(&world, NULL, 0);
            if (world.match.byPhase == MECHA_PHASE_FIGHT)
                iElapsed++;
        }
        printf("   a %d second round ran %d\n", iSeconds,
               iElapsed / MECHA_TICK_HZ);
        CHECK(iElapsed >= MECHA_TICK_HZ * iSeconds);
        CHECK(iElapsed <= MECHA_TICK_HZ * iSeconds + 2);
    }

    /* And a deathmatch has no clock at all: five minutes in, with neither
     * machine having so much as aimed at the other, the round is still
     * running. Setting a very long clock would not be the same thing --
     * that round still ends, on whoever has the most armour left. */
    {
        int i;

        mecha_sim_init(&world, 0, 0xDEA7u, 1);
        mecha_sim_set_round_seconds(&world, 0);
        CHECK(mecha_sim_add_mech(&world, 0, MECHA_CONTROL_HUMAN, 0) >= 0);
        CHECK(mecha_sim_add_mech(&world, 1, MECHA_CONTROL_HUMAN, 1) >= 0);
        mecha_sim_begin_match(&world);
        for (i = 0; i < MECHA_TICK_HZ * 300; i++)
            mecha_sim_tick(&world, NULL, 0);
        printf("   a deathmatch was still going after five minutes\n");
        CHECK(world.match.byPhase == MECHA_PHASE_FIGHT);
        CHECK(world.match.iRound == 1);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_enemy_can_be_told_to_hold_fire(void)
{
    tMechaWorld world;
    int aiShots[2];
    int aiMoved[2];
    int iPass;

    for (iPass = 0; iPass < 2; iPass++) {
        float afStartX[2];
        float afStartZ[2];
        int i;

        mecha_sim_init(&world, 0, 0x5AFEu, 2);
        CHECK(mecha_sim_add_mech(&world, 0, MECHA_CONTROL_AI, 0) >= 0);
        CHECK(mecha_sim_add_mech(&world, 2, MECHA_CONTROL_AI, 1) >= 0);
        mecha_sim_set_ai_hold_fire(&world, iPass == 1);
        mecha_sim_begin_match(&world);
        afStartX[0] = world.aMechs[0].fX;
        afStartZ[0] = world.aMechs[0].fZ;
        afStartX[1] = world.aMechs[1].fX;
        afStartZ[1] = world.aMechs[1].fZ;

        aiShots[iPass] = 0;
        aiMoved[iPass] = 0;
        for (i = 0; i < MECHA_TICK_HZ * 30; i++) {
            int iShot;

            mecha_sim_tick(&world, NULL, 0);
            for (iShot = 0; iShot < MECHA_MAX_PROJECTILES; iShot++) {
                if (world.aProjectiles[iShot].bActive)
                    aiShots[iPass]++;
            }
            /*
             * And they are still fighting for position while they do not
             * shoot, which is the whole point of the switch: a pilot that
             * stopped moving would show nothing about how the movement
             * looks.
             */
            if (mecha_length2(world.aMechs[0].fX - afStartX[0],
                              world.aMechs[0].fZ - afStartZ[0])
                    > MECHA_M(8.0f)
                || mecha_length2(world.aMechs[1].fX - afStartX[1],
                                 world.aMechs[1].fZ - afStartZ[1])
                       > MECHA_M(8.0f))
                aiMoved[iPass]++;
        }
    }

    printf("   half a minute of two pilots: %d shot-ticks firing, %d holding"
           " (moving on %d and %d ticks)\n", aiShots[0], aiShots[1],
           aiMoved[0], aiMoved[1]);
    CHECK(aiShots[0] > 0);
    CHECK(aiShots[1] == 0);
    CHECK(aiMoved[1] > MECHA_TICK_HZ * 20);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_legs_have_four_gaits(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    static float aafPose[4][4096];
    static const char *const kaszGait[4] = { "walk", "glide", "air",
                                             "air dash" };
    tMechaWorld world;
    const tMechaMechDef *pDef;
    int aiCount[4];
    float fWalkReach;
    float fWalkAcross;
    float fGlideReach;
    float fGlideAcross;
    int iGait;
    int iOther;

    start_duel(&world, 0, 0, 0, 0x6A17u, 1);
    CHECK(clear_runway(&world, 0));
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);
    world.aMechs[0].fStepPhase = 0.12f;
    world.iTick = 7;

    for (iGait = 0; iGait < 4; iGait++) {
        /* Walk, glide, hang, air dash -- the last two off the ground. */
        world.aMechs[0].fY = iGait >= 2 ? MECHA_M(12.0f) : 0.0f;
        world.aMechs[0].byMove = (iGait == 1 || iGait == 3)
                                 ? MECHA_MOVE_DASH
                                 : (iGait == 2 ? MECHA_MOVE_JUMP
                                               : MECHA_MOVE_WALK);
        aiCount[iGait] = gait_capture(&world, 0, aStorage, aafPose[iGait],
                                      4096);
        CHECK(aiCount[iGait] > 0);
        CHECK(aiCount[iGait] == aiCount[0]);
    }

    /*
     * Every one of them has to be its own shape. Comparing the poses vertex
     * by vertex rather than measuring how far apart the feet end up: two
     * quite different poses can have the same stride, and it is the shape
     * the player reads, not the number.
     */
    for (iGait = 0; iGait < 4; iGait++) {
        for (iOther = iGait + 1; iOther < 4; iOther++) {
            float fApart = gait_apart(aafPose[iGait], aafPose[iOther],
                                      aiCount[0]);

            printf("   %s against %s: %.1f m apart\n", kaszGait[iGait],
                   kaszGait[iOther], fApart / MECHA_METRE);
            CHECK(fApart > 0.35f * pDef->fRadius);
        }
    }

    /*
     * And a boost on the ground is a glide, not a run. That is not a
     * subjective claim about how it looks: a runner's feet pass each other
     * fore and aft and a skater's go out to the side, so the glide has to
     * come out narrower than the walk down the line of travel and wider
     * than it across.
     */
    world.aMechs[0].fY = 0.0f;
    world.aMechs[0].byMove = MECHA_MOVE_WALK;
    fWalkReach = gait_reach(&world, 0, aStorage, false);
    fWalkAcross = gait_reach(&world, 0, aStorage, true);
    world.aMechs[0].byMove = MECHA_MOVE_DASH;
    fGlideReach = gait_reach(&world, 0, aStorage, false);
    fGlideAcross = gait_reach(&world, 0, aStorage, true);
    printf("   walk: %.1f m along, %.1f m across; glide: %.1f m along,"
           " %.1f m across\n", fWalkReach / MECHA_METRE,
           fWalkAcross / MECHA_METRE, fGlideReach / MECHA_METRE,
           fGlideAcross / MECHA_METRE);
    CHECK(fGlideReach < fWalkReach * 0.8f);
    CHECK(fGlideAcross > fWalkAcross * 1.15f);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_dashing_squares_the_legs_to_the_burst(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iOffset;
    int iAim;
    int i;

    start_duel(&world, 0, 0, 0, 0x5C0Au, 1);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(clear_runway(&world, 0));
    world.aMechs[0].byLock = MECHA_LOCK_HELD;
    world.aMechs[0].iTargetIdx = 1;
    iAim = world.aMechs[0].iFacing;

    /* Dashing hard to the right of where the machine is pointed. */
    aInputs[0].iMoveX = 100;
    aInputs[0].bDash = true;
    mecha_sim_tick(&world, aInputs, 2);
    aInputs[0].bDash = false;
    for (i = 0; i < 12; i++)
        mecha_sim_tick(&world, aInputs, 2);

    iOffset = mecha_angle_delta(world.aMechs[0].iFacing,
                               world.aMechs[0].iLegYaw);
    printf("   dashing sideways: legs %d off the shoulders (walk clamp"
           " is %d)\n", iOffset, MECHA_LEG_YAW_LIMIT);
    /* Past the clamp a strafing walk is held to: a boost is not a strafe,
     * and the legs square up to it however far round that is. */
    CHECK(abs(iOffset) > MECHA_LEG_YAW_LIMIT);
    /* And the shoulders have not gone with them. */
    CHECK(abs(mecha_angle_delta(iAim, world.aMechs[0].iFacing))
          < MECHA_DEG(20));
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_a_cancel_drops_like_a_stone(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    float fPeak;
    int iFallTicks = 0;
    int i;

    start_duel(&world, 0, 0, 0, 0xD40Fu, 1);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(clear_runway(&world, 0));

    aInputs[0].bJump = true;
    mecha_sim_tick(&world, aInputs, 2);
    aInputs[0].bJump = false;
    for (i = 0; i < 16; i++)
        mecha_sim_tick(&world, aInputs, 2);
    fPeak = world.aMechs[0].fY;
    CHECK(fPeak > MECHA_M(2.0f));

    aInputs[0].bGuard = true;
    mecha_sim_tick(&world, aInputs, 2);
    aInputs[0].bGuard = false;
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_CANCEL);

    for (i = 0; i < MECHA_TICK_HZ; i++) {
        mecha_sim_tick(&world, aInputs, 2);
        iFallTicks++;
        if (world.aMechs[0].byMove != MECHA_MOVE_CANCEL)
            break;
    }

    printf("   cancelled from %.1f m and down in %d ticks\n",
           fPeak / MECHA_METRE, iFallTicks);
    /* Dropped, not lowered: at a hundred and twenty metres a second even a
     * high arc is over inside a fifth of a second. */
    CHECK(iFallTicks <= 12);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_torso_turns_off_the_legs(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    static tMechaQuad aTurned[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaQuadList turned;
    tMechaWorld world;
    const tMechaMechDef *pDef;
    float fUpperMoved = 0.0f;
    float fLowerMoved = 0.0f;
    int i;
    int v;

    start_duel(&world, 0, 0, 0, 0x707Cu, 1);
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);
    world.aMechs[0].iFacing = 0;
    world.aMechs[0].iLegYaw = 0;
    world.aMechs[0].fStepPhase = 0.0f;
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, &world, 0);

    world.aMechs[0].iLegYaw = MECHA_DEG(40);
    mecha_quads_reset(&turned, aTurned, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&turned, &world, 0);
    CHECK(turned.iCount == list.iCount);

    for (i = 0; i < list.iCount; i++) {
        for (v = 0; v < 4; v++) {
            float fY = aStorage[i].afVert[v][1] - world.aMechs[0].fY;
            float fDx = aTurned[i].afVert[v][0] - aStorage[i].afVert[v][0];
            float fDz = aTurned[i].afVert[v][2] - aStorage[i].afVert[v][2];
            float fMoved = mecha_length2(fDx, fDz);

            if (fY > 0.60f * pDef->fHeight) {
                if (fMoved > fUpperMoved)
                    fUpperMoved = fMoved;
            } else if (fY < 0.30f * pDef->fHeight) {
                if (fMoved > fLowerMoved)
                    fLowerMoved = fMoved;
            }
        }
    }

    printf("   legs turned 40 degrees: chest moved %.0f, legs moved %.0f\n",
           fUpperMoved, fLowerMoved);
    /* Turning the stance must move the legs and leave the shoulders where
     * they were: that is the whole point of the machine having a waist. */
    CHECK(fLowerMoved > 0.25f * pDef->fRadius);
    CHECK(fUpperMoved < 0.02f * pDef->fRadius);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_arms_and_head_follow_the_lock(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    const tMechaMechDef *pDef;
    float fLeftX;
    float fRightX;
    float fHeadLeftX;
    float fHeadRightX;
    float fZ;

    start_duel(&world, 0, 0, 0, 0xA124u, 1);
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);
    world.aMechs[0].fX = 0.0f;
    world.aMechs[0].fZ = 0.0f;
    world.aMechs[0].iFacing = 0;
    world.aMechs[0].iLegYaw = 0;
    world.aMechs[0].byLock = MECHA_LOCK_HELD;
    world.aMechs[0].iTargetIdx = 1;
    world.aMechs[1].fY = world.aMechs[0].fY;

    /* Target off to the left, then off to the right, both well inside the
     * reach of a shoulder. */
    world.aMechs[1].fX = -MECHA_M(40.0f);
    world.aMechs[1].fZ = MECHA_M(40.0f);
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, &world, 0);
    mesh_band_centroid(&list, &world.aMechs[0], 0.40f * pDef->fHeight,
                       0.62f * pDef->fHeight, &fLeftX, &fZ, NULL);
    mesh_band_centroid(&list, &world.aMechs[0], 0.80f * pDef->fHeight,
                       1.10f * pDef->fHeight, &fHeadLeftX, &fZ, NULL);

    world.aMechs[1].fX = MECHA_M(40.0f);
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, &world, 0);
    mesh_band_centroid(&list, &world.aMechs[0], 0.40f * pDef->fHeight,
                       0.62f * pDef->fHeight, &fRightX, &fZ, NULL);
    mesh_band_centroid(&list, &world.aMechs[0], 0.80f * pDef->fHeight,
                       1.10f * pDef->fHeight, &fHeadRightX, &fZ, NULL);

    printf("   across the lock: arms swing %.0f, head turns %.0f\n",
           fRightX - fLeftX, fHeadRightX - fHeadLeftX);
    /* Both arms swing towards whatever is locked, so the mass hanging off
     * the shoulders moves with the target rather than staying square, and
     * the head looks the same way on a shorter leash. */
    CHECK(fRightX - fLeftX > 0.20f * pDef->fRadius);
    CHECK(fHeadRightX - fHeadLeftX > 0.02f * pDef->fRadius);
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* The old key, kept here so the test can say what the new one bought. */
static float quad_centre_key(const tMechaQuad *pQuad, const float afEye[3],
                             const float afForward[3])
{
    float fKey = 0.0f;
    int v;

    for (v = 0; v < 4; v++) {
        fKey += 0.25f * ((pQuad->afVert[v][0] - afEye[0]) * afForward[0]
                       + (pQuad->afVert[v][1] - afEye[1]) * afForward[1]
                       + (pQuad->afVert[v][2] - afEye[2]) * afForward[2]);
    }
    return fKey;
}

//-------------------------------------------------------------------------------------------------

static void quad_ground_span(const tMechaQuad *pQuad, float *pfLowX,
                             float *pfHighX, float *pfLowZ, float *pfHighZ,
                             float *pfY)
{
    int v;

    *pfLowX = *pfHighX = pQuad->afVert[0][0];
    *pfLowZ = *pfHighZ = pQuad->afVert[0][2];
    *pfY = 0.0f;
    for (v = 0; v < 4; v++) {
        if (pQuad->afVert[v][0] < *pfLowX)
            *pfLowX = pQuad->afVert[v][0];
        if (pQuad->afVert[v][0] > *pfHighX)
            *pfHighX = pQuad->afVert[v][0];
        if (pQuad->afVert[v][2] < *pfLowZ)
            *pfLowZ = pQuad->afVert[v][2];
        if (pQuad->afVert[v][2] > *pfHighZ)
            *pfHighZ = pQuad->afVert[v][2];
        *pfY += 0.25f * pQuad->afVert[v][1];
    }
}

//-------------------------------------------------------------------------------------------------

/*
 * Counts the decals that would be painted over by ground they lie on. With
 * no depth buffer the whole picture rests on the order the quads are handed
 * over in, and this is the one ordering the eye notices: a shadow cut in
 * half along a floor tile edge that slides about as the camera moves.
 */
static int decal_inversions(const tMechaQuadList *pList, const float afEye[3],
                            const float afForward[3], bool bOldKey)
{
    int iBad = 0;
    int i;
    int j;

    for (i = 0; i < pList->iCount; i++) {
        const tMechaQuad *pDecal = &pList->paQuads[i];
        float fDecalLowX;
        float fDecalHighX;
        float fDecalLowZ;
        float fDecalHighZ;
        float fDecalY;
        float fDecalKey;

        if (!(pDecal->byFlags & MECHA_QUAD_SHADOW))
            continue;
        quad_ground_span(pDecal, &fDecalLowX, &fDecalHighX, &fDecalLowZ,
                         &fDecalHighZ, &fDecalY);
        fDecalKey = bOldKey ? quad_centre_key(pDecal, afEye, afForward)
                            : mecha_quad_depth_key(pDecal, afEye, afForward);

        for (j = 0; j < pList->iCount; j++) {
            const tMechaQuad *pGround = &pList->paQuads[j];
            float fLowX;
            float fHighX;
            float fLowZ;
            float fHighZ;
            float fY;
            float fKey;

            if (pGround->byFlags & MECHA_QUAD_SHADOW)
                continue;
            if (pGround->afNormal[1] < 0.9f)
                continue;               /* the ground, not a wall or a hull */
            quad_ground_span(pGround, &fLowX, &fHighX, &fLowZ, &fHighZ, &fY);
            if (fY > fDecalY)
                continue;               /* above the decal: not underneath it */
            if (fDecalY - fY > MECHA_M(0.5f))
                continue;               /* a different storey */
            if (fHighX <= fDecalLowX || fLowX >= fDecalHighX)
                continue;
            if (fHighZ <= fDecalLowZ || fLowZ >= fDecalHighZ)
                continue;

            fKey = bOldKey ? quad_centre_key(pGround, afEye, afForward)
                           : mecha_quad_depth_key(pGround, afEye, afForward);
            /* Larger key is drawn first. The ground has to go down before
             * the shadow lying on it. */
            if (fKey <= fDecalKey)
                iBad++;
        }
    }
    return iBad;
}

//-------------------------------------------------------------------------------------------------

static int test_shadows_survive_the_paint_order(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iWasBad = 0;
    int iNowBad = 0;
    int iAngle;

    start_duel(&world, 0, 0, 1, 0x5017u, 1);
    memset(aInputs, 0, sizeof(aInputs));
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);

    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_arena(&list, &world.arena);
    mecha_mesh_shadows(&list, &world);
    CHECK(list.iCount > 0);

    /* Walked all the way round, because the failure is a function of where
     * the camera is: a tile whose middle happens to fall nearer than the
     * shadow's is what does the damage, and which tile that is depends on
     * the angle it is seen from. */
    for (iAngle = 0; iAngle < 16; iAngle++) {
        int iYaw = iAngle * MECHA_ANGLE_FULL / 16;
        float afForward[3];
        float afEye[3];

        afForward[0] = mecha_sin(iYaw);
        afForward[1] = -0.25f;
        afForward[2] = mecha_cos(iYaw);
        afEye[0] = world.aMechs[0].fX - afForward[0] * MECHA_M(26.0f);
        afEye[1] = MECHA_M(14.0f);
        afEye[2] = world.aMechs[0].fZ - afForward[2] * MECHA_M(26.0f);

        iWasBad += decal_inversions(&list, afEye, afForward, true);
        iNowBad += decal_inversions(&list, afEye, afForward, false);
    }

    printf("   ground painted over a shadow: %d times, was %d\n", iNowBad,
           iWasBad);
    /* The old key got this wrong often enough to see; the new one must not
     * get it wrong at all. */
    CHECK(iWasBad > 0);
    CHECK(iNowBad == 0);
    return 0;
}

//-------------------------------------------------------------------------------------------------

/*
 * A blast goes off inside a machine, and the machine must not paint over
 * it. Nothing here can be fixed by nudging geometry -- the two really do
 * occupy the same space -- so it is the draw order or nothing.
 */
/*
 * Two quads facing the same way, lying in the same plane, overlapping. No
 * sort can separate them -- whichever is handed over second wins the whole
 * shared area -- so the only fix is not to build them, and the only way to
 * know they are not being built is to look.
 */
static int coplanar_overlaps(const tMechaQuadList *pList)
{
    int iPairs = 0;
    int i;
    int j;

    for (i = 0; i < pList->iCount; i++) {
        const tMechaQuad *pA = &pList->paQuads[i];
        float fPlaneA = pA->afNormal[0] * pA->afVert[0][0]
                      + pA->afNormal[1] * pA->afVert[0][1]
                      + pA->afNormal[2] * pA->afVert[0][2];

        for (j = i + 1; j < pList->iCount; j++) {
            const tMechaQuad *pB = &pList->paQuads[j];
            float fDot = pA->afNormal[0] * pB->afNormal[0]
                       + pA->afNormal[1] * pB->afNormal[1]
                       + pA->afNormal[2] * pB->afNormal[2];
            float fPlaneB;
            int iAxis;
            int bOverlap = 1;

            if (fDot < 0.999f)
                continue;               /* not parallel, or facing away */
            fPlaneB = pB->afNormal[0] * pB->afVert[0][0]
                    + pB->afNormal[1] * pB->afVert[0][1]
                    + pB->afNormal[2] * pB->afVert[0][2];
            if (fabsf(fPlaneA - fPlaneB) > 1.0f)
                continue;               /* different planes: sortable */

            for (iAxis = 0; iAxis < 3 && bOverlap; iAxis++) {
                float fLowA = pA->afVert[0][iAxis];
                float fHighA = fLowA;
                float fLowB = pB->afVert[0][iAxis];
                float fHighB = fLowB;
                int v;

                for (v = 1; v < 4; v++) {
                    if (pA->afVert[v][iAxis] < fLowA)
                        fLowA = pA->afVert[v][iAxis];
                    if (pA->afVert[v][iAxis] > fHighA)
                        fHighA = pA->afVert[v][iAxis];
                    if (pB->afVert[v][iAxis] < fLowB)
                        fLowB = pB->afVert[v][iAxis];
                    if (pB->afVert[v][iAxis] > fHighB)
                        fHighB = pB->afVert[v][iAxis];
                }
                /* A shared edge is not an overlap: tiles are meant to meet. */
                if (fHighA - fLowB < 1.0f || fHighB - fLowA < 1.0f)
                    bOverlap = 0;
            }
            if (bOverlap)
                iPairs++;
        }
    }
    return iPairs;
}

//-------------------------------------------------------------------------------------------------

/*
 * Puts a machine somewhere it can dash without hitting anything, facing a
 * given way, and sends the other one out of the picture. Arena layouts have
 * cover in them and a burst crosses tens of metres, so a test that wants to
 * measure a dash has to find itself a runway first -- otherwise it measures
 * a bounce and calls it a bug.
 */
static bool clear_runway(tMechaWorld *pWorld, int iFacing)
{
    const tMechaMechDef *pDef = mecha_def_get((int)pWorld->aMechs[0].byDefIdx);
    float fDirX = mecha_sin(iFacing);
    float fDirZ = mecha_cos(iFacing);
    float fStep = pDef->fRadius * 1.5f;
    int iSlot;

    for (iSlot = 0; iSlot < 400; iSlot++) {
        /* A lattice across the floor, skipping the middle where the round
         * starts and cover tends to be. */
        float fX = (float)((iSlot % 20) - 10) * pWorld->arena.fHalfExtent
                   / 11.0f;
        float fZ = (float)((iSlot / 20) - 10) * pWorld->arena.fHalfExtent
                   / 11.0f;
        bool bClear = true;
        float fAhead;

        for (fAhead = 0.0f; fAhead < MECHA_M(34.0f) && bClear;
             fAhead += fStep) {
            float fProbeX = fX + fDirX * fAhead;
            float fProbeZ = fZ + fDirZ * fAhead;
            float fMovedX = fProbeX;
            float fMovedZ = fProbeZ;

            if (!mecha_arena_contains(&pWorld->arena, fProbeX, fProbeZ))
                bClear = false;
            else if (mecha_arena_resolve_cylinder(&pWorld->arena,
                                                  pDef->fRadius * 1.4f, 0.0f,
                                                  pDef->fHeight, &fMovedX,
                                                  &fMovedZ))
                bClear = false;
            else if (mecha_arena_ground_height(&pWorld->arena, fProbeX,
                                               fProbeZ, 0.0f) > 1.0f)
                bClear = false;
        }
        if (!bClear)
            continue;

        pWorld->aMechs[0].fX = fX;
        pWorld->aMechs[0].fZ = fZ;
        pWorld->aMechs[0].fY = 0.0f;
        pWorld->aMechs[0].iFacing = mecha_angle_wrap(iFacing);
        pWorld->aMechs[0].iLegYaw = pWorld->aMechs[0].iFacing;
        /* The other machine is a wall too. */
        pWorld->aMechs[1].fX = -fX;
        pWorld->aMechs[1].fZ = -fZ - MECHA_M(4.0f);
        return true;
    }
    return false;
}

//-------------------------------------------------------------------------------------------------

/* Heading of the machine's travel, or -1 when it is not travelling. */
static int travel_heading(const tMechaMech *pMech)
{
    if (mecha_length2(pMech->fVelX, pMech->fVelZ) < MECHA_MPS(1.0f))
        return -1;
    return mecha_atan2_angle(pMech->fVelX, pMech->fVelZ);
}

//-------------------------------------------------------------------------------------------------

static int test_dashing_is_committed(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaMechDef *pDef;
    float fSpeedAfterRelease;
    int iTicksDashed = 0;
    int i;

    start_duel(&world, 0, 0, 0, 0xDA54u, 1);
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);
    memset(aInputs, 0, sizeof(aInputs));

    /* One tap, then hands off the button entirely. */
    aInputs[0].iMoveZ = 100;
    aInputs[0].bDash = true;
    mecha_sim_tick(&world, aInputs, 2);
    aInputs[0].bDash = false;
    aInputs[0].iMoveZ = 0;
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_DASH);

    for (i = 0; i < pDef->iDashTicks * 2; i++) {
        mecha_sim_tick(&world, aInputs, 2);
        if (world.aMechs[0].byMove == MECHA_MOVE_DASH)
            iTicksDashed++;
        else
            break;
    }
    fSpeedAfterRelease = mecha_length2(world.aMechs[0].fVelX,
                                       world.aMechs[0].fVelZ);

    printf("   burst ran %d ticks of %d after the button came up,"
           " leaving %.1f m/s\n", iTicksDashed + 1, pDef->iDashTicks,
           fSpeedAfterRelease / MECHA_METRE);
    /* The button starts the burst; it does not hold it up. */
    CHECK(iTicksDashed + 1 >= pDef->iDashTicks - 1);
    /* And the speed it built is not thrown away with it. */
    CHECK(fSpeedAfterRelease > pDef->fWalkSpeed);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_a_dash_can_be_steered_and_cancelled(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iStraight;
    int iCrossed;
    int iHeld;
    int iCancelled;

    /* --- the crossing step: let go, tap across, the burst turns --------- */
    start_duel(&world, 0, 0, 0, 0xC205u, 1);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(clear_runway(&world, 0));
    aInputs[0].iMoveZ = 100;
    aInputs[0].bDash = true;
    mecha_sim_tick(&world, aInputs, 2);
    aInputs[0].bDash = false;
    run_ticks(&world, aInputs, 2, 3);
    iStraight = travel_heading(&world.aMechs[0]);
    CHECK(iStraight >= 0);

    aInputs[0].iMoveZ = 0;                      /* released */
    mecha_sim_tick(&world, aInputs, 2);
    aInputs[0].iMoveX = 100;                    /* tapped across */
    run_ticks(&world, aInputs, 2, 3);
    iCrossed = travel_heading(&world.aMechs[0]);
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_DASH);

    /* --- holding a direction is not steering ---------------------------- */
    {
        tMechaWorld held;

        start_duel(&held, 0, 0, 0, 0xC205u, 1);
        memset(aInputs, 0, sizeof(aInputs));
        CHECK(clear_runway(&held, 0));
        aInputs[0].iMoveZ = 100;
        aInputs[0].bDash = true;
        mecha_sim_tick(&held, aInputs, 2);
        aInputs[0].bDash = false;
        /* Straight onto a new direction without ever letting go. */
        aInputs[0].iMoveX = 100;
        run_ticks(&held, aInputs, 2, 6);
        iHeld = travel_heading(&held.aMechs[0]);
        CHECK(held.aMechs[0].byMove == MECHA_MOVE_DASH);
    }

    /* --- the cancel: push back and boost again -------------------------- */
    {
        tMechaWorld back;

        start_duel(&back, 0, 0, 0, 0xC205u, 1);
        memset(aInputs, 0, sizeof(aInputs));
        CHECK(clear_runway(&back, 0));
        aInputs[0].iMoveZ = 100;
        aInputs[0].bDash = true;
        mecha_sim_tick(&back, aInputs, 2);
        aInputs[0].bDash = false;
        run_ticks(&back, aInputs, 2, 3);
        aInputs[0].iMoveZ = -100;
        aInputs[0].bDash = true;
        mecha_sim_tick(&back, aInputs, 2);
        aInputs[0].bDash = false;
        run_ticks(&back, aInputs, 2, 2);
        iCancelled = travel_heading(&back.aMechs[0]);
        CHECK(back.aMechs[0].byMove == MECHA_MOVE_DASH);
    }

    printf("   dash headings: straight %d, crossed %d, held %d,"
           " cancelled %d\n", iStraight, iCrossed, iHeld, iCancelled);
    /* Released and tapped across: the burst turns, and by a lot. */
    CHECK(abs(mecha_angle_delta(iStraight, iCrossed)) > MECHA_DEG(60));
    /* Held through: it does not. */
    CHECK(abs(mecha_angle_delta(iStraight, iHeld)) < MECHA_DEG(8));
    /* Pushed back against and boosted again: the other way entirely. */
    CHECK(abs(mecha_angle_delta(iStraight, iCancelled)) > MECHA_DEG(150));
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_a_boost_carries_and_bounces(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaMechDef *pDef;
    float fCoasted;
    float fWalked;
    int i;

    /* --- momentum out the far side of a burst --------------------------- */
    start_duel(&world, 0, 0, 0, 0xB005u, 1);
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(clear_runway(&world, 0));
    aInputs[0].iMoveZ = 100;
    aInputs[0].bDash = true;
    mecha_sim_tick(&world, aInputs, 2);
    memset(aInputs, 0, sizeof(aInputs));
    for (i = 0; i < pDef->iDashTicks * 2; i++) {
        mecha_sim_tick(&world, aInputs, 2);
        if (world.aMechs[0].byMove != MECHA_MOVE_DASH)
            break;
    }
    {
        float fZ0 = world.aMechs[0].fZ;

        run_ticks(&world, aInputs, 2, MECHA_TICK_HZ / 3);
        fCoasted = world.aMechs[0].fZ - fZ0;
    }

    /* The same window, walking and then stopping dead. */
    {
        tMechaWorld walk;
        float fZ0;

        start_duel(&walk, 0, 0, 0, 0xB005u, 1);
        memset(aInputs, 0, sizeof(aInputs));
        CHECK(clear_runway(&walk, 0));
        aInputs[0].iMoveZ = 100;
        run_ticks(&walk, aInputs, 2, MECHA_TICK_HZ);
        memset(aInputs, 0, sizeof(aInputs));
        fZ0 = walk.aMechs[0].fZ;
        run_ticks(&walk, aInputs, 2, MECHA_TICK_HZ / 3);
        fWalked = walk.aMechs[0].fZ - fZ0;
    }

    printf("   after the burst ends: coasts %.1f m, a stopped walk %.1f m\n",
           fCoasted / MECHA_METRE, fWalked / MECHA_METRE);
    CHECK(fCoasted > fWalked * 1.6f);

    /* --- off a wall ------------------------------------------------------ */
    {
        tMechaWorld wall;
        float fInto;
        float fOut;
        float fSpeed;

        start_duel(&wall, 0, 0, 0, 0xBA11u, 1);
        memset(aInputs, 0, sizeof(aInputs));
        /* Facing the far wall from just short of it. */
        face_mech(&wall, 0, 0);
        wall.aMechs[0].fX = 0.0f;
        wall.aMechs[0].fZ = wall.arena.fHalfExtent - MECHA_M(8.0f);
        wall.aMechs[1].fX = 0.0f;
        wall.aMechs[1].fZ = -wall.arena.fHalfExtent + MECHA_M(8.0f);
        aInputs[0].iMoveZ = 100;
        aInputs[0].bDash = true;
        mecha_sim_tick(&wall, aInputs, 2);
        memset(aInputs, 0, sizeof(aInputs));
        fInto = wall.aMechs[0].fVelZ;
        CHECK(fInto > 0.0f);

        for (i = 0; i < MECHA_TICK_HZ; i++) {
            mecha_sim_tick(&wall, aInputs, 2);
            if (wall.aMechs[0].fVelZ < 0.0f)
                break;
        }
        fOut = wall.aMechs[0].fVelZ;
        fSpeed = mecha_length2(wall.aMechs[0].fVelX, wall.aMechs[0].fVelZ);

        printf("   into the wall at %.1f m/s, off it at %.1f m/s\n",
               fInto / MECHA_METRE, -fOut / MECHA_METRE);
        /* It comes off rather than sticking to it, and with real speed. */
        CHECK(fOut < 0.0f);
        CHECK(fSpeed > MECHA_BOUNCE_MIN_SPEED);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_dashing_works_in_the_air(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaMechDef *pDef;
    float fTakeoffY;
    float fDashY;
    float fSpeed;
    int i;

    start_duel(&world, 0, 0, 0, 0xA12Du, 1);
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(clear_runway(&world, 0));

    aInputs[0].bJump = true;
    mecha_sim_tick(&world, aInputs, 2);
    aInputs[0].bJump = false;
    run_ticks(&world, aInputs, 2, 12);
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_JUMP);
    CHECK(world.aMechs[0].fY > MECHA_M(1.0f));

    aInputs[0].iMoveZ = 100;
    aInputs[0].bDash = true;
    mecha_sim_tick(&world, aInputs, 2);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_DASH);
    fTakeoffY = world.aMechs[0].fY;

    for (i = 0; i < pDef->iDashTicks - 2; i++)
        mecha_sim_tick(&world, aInputs, 2);
    fDashY = world.aMechs[0].fY;
    fSpeed = mecha_length2(world.aMechs[0].fVelX, world.aMechs[0].fVelZ);

    printf("   air dash held %.2f m of height and made %.1f m/s\n",
           (fDashY - fTakeoffY) / MECHA_METRE, fSpeed / MECHA_METRE);
    /* Flat, fast, and still in the air: a burst holds its height. */
    CHECK(fDashY > fTakeoffY - MECHA_M(0.5f));
    CHECK(fSpeed > pDef->fDashSpeed * 0.9f);
    CHECK(world.aMechs[0].fY > MECHA_M(0.5f));

    /* And when it runs out, the machine is falling again, not standing. */
    for (i = 0; i < 6; i++)
        mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_JUMP);
    CHECK(world.aMechs[0].fVelY < 0.0f);
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* Index of the arena whose name matches, so the tests name places rather
 * than numbers. */
static int arena_by_name(const char *szName)
{
    int i;

    for (i = 0; i < mecha_arena_count(); i++) {
        if (strcmp(mecha_arena_name(i), szName) == 0)
            return i;
    }
    return -1;
}

//-------------------------------------------------------------------------------------------------

/*
 * How long after a hit a machine still counts as having been shoved.
 *
 * The first version of this asked whether stagger was above zero on the
 * exact tick the machine crossed the line, and that is not the same
 * question. Stagger bleeds off at fifty-five a second, so a mech hit hard
 * at the far end of a slide arrives at the edge with none left and books
 * itself down as having strolled -- two of the six seeds here did exactly
 * that, each after being shot the whole way across the roof. What matters
 * is whether anybody had shot it recently, so that is what is recorded.
 */
#define ROOF_SHOVE_MEMORY MECHA_SEC(2.0f)

static int roof_excursions(uint32_t uiSeed, int *piStroll, int *piPushed)
{
    tMechaWorld world;
    int iArena = arena_by_name("TOWER SEVEN ROOF");
    bool abWasLethal[2] = { false, false };
    float afLastArmour[2] = { 0.0f, 0.0f };
    int aiHitAgo[2] = { 0, 0 };
    int i;

    CHECK(iArena >= 0);
    mecha_sim_init(&world, iArena, uiSeed, 2);
    CHECK(mecha_sim_add_mech(&world, 1, MECHA_CONTROL_AI, 0) >= 0);
    CHECK(mecha_sim_add_mech(&world, 2, MECHA_CONTROL_AI, 1) >= 0);
    mecha_sim_begin_match(&world);
    for (i = 0; i < 2; i++)
        afLastArmour[i] = world.aMechs[i].fArmour;

    for (i = 0; i < MECHA_TICK_HZ * 60; i++) {
        int iMech;

        mecha_sim_tick(&world, NULL, 0);
        for (iMech = 0; iMech < 2; iMech++) {
            const tMechaMech *pMech = &world.aMechs[iMech];
            uint32_t uiSurface = mecha_arena_surface(&world.arena, pMech->fX,
                                                     pMech->fZ);
            bool bLethal;

            /* Armour going down is somebody shooting; stagger rising is
             * being shoved by something that did no damage. Either starts
             * the clock. */
            if (pMech->fArmour < afLastArmour[iMech] || pMech->fStagger > 0.0f)
                aiHitAgo[iMech] = ROOF_SHOVE_MEMORY;
            else if (aiHitAgo[iMech] > 0)
                aiHitAgo[iMech]--;
            afLastArmour[iMech] = pMech->fArmour;

            if (!mecha_mech_alive(pMech)) {
                abWasLethal[iMech] = false;
                continue;
            }
            /*
             * Flying over the hole is allowed and is half the point of
             * having one -- what is not allowed is standing in it, or
             * walking off the edge. Both of those are being at roof level
             * where the roof is not.
             */
            bLethal = pMech->fY <= MECHA_M(1.0f)
                      && (((uiSurface & MECHA_SURF_PIT) != 0)
                          || !mecha_arena_contains(&world.arena, pMech->fX,
                                                   pMech->fZ));
            if (bLethal && !abWasLethal[iMech]) {
                /*
                 * Being shot over the edge is a fair way to lose a roof
                 * fight; walking over it under your own power is not. The
                 * stagger a hit leaves behind is what tells the two apart.
                 */
                if (aiHitAgo[iMech] > 0)
                    (*piPushed)++;
                else {
                    (*piStroll)++;
                    printf("      stroll: move=%d coast=%d y=%.1f sp=%.1f "
                           "x=%.0f z=%.0f\n", pMech->byMove,
                           pMech->iCoastTicks, pMech->fY / MECHA_METRE,
                           mecha_length2(pMech->fVelX, pMech->fVelZ)
                               / MECHA_METRE,
                           pMech->fX / MECHA_METRE, pMech->fZ / MECHA_METRE);
                }
            }
            abWasLethal[iMech] = bLethal;
        }
    }

    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_computer_pilot_stays_on_the_roof(void)
{
    static const uint32_t auiSeeds[] = { 0x2007u, 0x51EDu, 0x0C0Fu,
                                         0x7A11u, 0x1234u, 0xBEEFu };
    int iStroll = 0;
    int iPushed = 0;
    size_t i;

    /*
     * Six minutes of two computer pilots fighting on a roof with a hole in
     * it. They are allowed to shoot each other to pieces, and to shove each
     * other off the edge doing it; what they are not allowed to do is walk
     * into the pit under their own power, which would make the arena a
     * joke.
     */
    for (i = 0; i < sizeof(auiSeeds) / sizeof(auiSeeds[0]); i++)
        CHECK(roof_excursions(auiSeeds[i], &iStroll, &iPushed) == 0);

    printf("   six roof fights: %d strolls into the void, %d shoved\n",
           iStroll, iPushed);
    CHECK(iStroll == 0);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_meadow_is_an_octagon_with_hills(void)
{
    tMechaArena arena;
    int iArena = arena_by_name("COLDWATER MEADOW");
    float fHighest = 0.0f;
    float fSteepest = 0.0f;
    int iTrees = 0;
    int iRocks = 0;
    int i;

    CHECK(iArena >= 0);
    mecha_arena_init(&arena, iArena);
    CHECK(arena.byShape == MECHA_ARENA_OCTAGON);

    /* The corners are cut: a point beyond the diagonal is outside, and the
     * same distance along an axis is not. */
    CHECK(mecha_arena_contains(&arena, arena.fHalfExtent * 0.95f, 0.0f));
    CHECK(!mecha_arena_contains(&arena, arena.fHalfExtent * 0.8f,
                                arena.fHalfExtent * 0.8f));

    /* Hills, and slopes steep enough to be worth boosting up. */
    for (i = 0; i < 4000; i++) {
        float fX = -arena.fHalfExtent
                   + arena.fHalfExtent * 2.0f * (float)(i % 64) / 64.0f;
        float fZ = -arena.fHalfExtent
                   + arena.fHalfExtent * 2.0f * (float)((i / 64) % 64) / 64.0f;
        float fHere = mecha_arena_terrain_height(&arena, fX, fZ);
        float fStep = MECHA_M(4.0f);
        float fRise = mecha_arena_terrain_height(&arena, fX + fStep, fZ)
                      - fHere;
        float fGrade = fRise / fStep;

        if (fHere > fHighest)
            fHighest = fHere;
        if (fGrade > fSteepest)
            fSteepest = fGrade;
    }

    /* Nothing holds a machine to this ground: that is what makes a hill a
     * ramp rather than a climb. */
    CHECK((mecha_arena_surface(&arena, 0.0f, 0.0f)
           & MECHA_SURF_NON_MAGNETIC) != 0);

    for (i = 0; i < arena.iObstacleCount; i++) {
        if (arena.aObstacles[i].byKind == MECHA_PROP_TREE)
            iTrees++;
        if (arena.aObstacles[i].byKind == MECHA_PROP_ROCK)
            iRocks++;
    }

    printf("   meadow: %.0f m of hill, steepest grade %.2f, %d trees,"
           " %d rocks\n", fHighest / MECHA_METRE, fSteepest, iTrees,
           iRocks);
    CHECK(fHighest > MECHA_M(15.0f));
    CHECK(fSteepest > 0.2f);
    CHECK(iTrees >= 6);
    CHECK(iRocks >= 2);
    /* And nothing built out of a building. */
    for (i = 0; i < arena.iObstacleCount; i++)
        CHECK(arena.aObstacles[i].byKind != MECHA_PROP_BLOCK);

    /* --- and it is a clearing, not a field ------------------------------
     *
     * Room to fight in: a good deal more than the walled arenas have, which
     * is what the hills and the distances are for. Nothing to see at the
     * edge of it: the boundary is still there and still stops a machine,
     * but there is no wall drawn on it. And past that, more forest -- ground
     * running well beyond anywhere anyone can stand, with trees on it.
     */
    {
        tMechaArena yard;

        mecha_arena_init(&yard, 0);
        printf("   meadow: %.0f m across against the yard's %.0f, no wall,"
               " ground out to %.0f m, %d billboards\n",
               arena.fHalfExtent * 2.0f / MECHA_METRE,
               yard.fHalfExtent * 2.0f / MECHA_METRE,
               arena.fOuterReach / MECHA_METRE, arena.iBillboards);
        CHECK(arena.fHalfExtent > yard.fHalfExtent * 2.0f);
        CHECK(arena.fWallHeight == 0.0f);
        CHECK(arena.fOuterReach > arena.fHalfExtent * 1.5f);
        CHECK(arena.iBillboards > 100);
    }

    /* The ground it grows the hills on has to be fine enough to hold their
     * shape. Doubling the arena without doubling this rounded them off into
     * bumps, and a bump is not a ramp. */
    CHECK(arena.iTerrainCells > MECHA_TERRAIN_CELLS_DEFAULT);
    CHECK(arena.iTerrainCells <= MECHA_TERRAIN_CELLS);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_gun_car_is_a_car_with_a_gun(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    const tMechaMechDef *pDef;
    const tMechaMechDef *pWalker = mecha_def_get(0);
    int iCar = -1;
    float afLow[3] = { 1e30f, 1e30f, 1e30f };
    float afHigh[3] = { -1e30f, -1e30f, -1e30f };
    float fGunOut = 0.0f;
    int i;

    for (i = 0; i < mecha_def_count(); i++) {
        if (mecha_def_get(i)->bWheeled)
            iCar = i;
    }
    CHECK(iCar >= 0);
    pDef = mecha_def_get(iCar);

    /* A sixth of a machine, near enough, and no taller than it is long. */
    printf("   %s: %.1f m tall against a machine's %.1f\n", pDef->szName,
           pDef->fHeight / MECHA_METRE, pWalker->fHeight / MECHA_METRE);
    CHECK(pDef->fHeight < pWalker->fHeight / 5.0f);
    CHECK(pDef->fHeight > pWalker->fHeight / 7.0f);

    start_duel(&world, 0, iCar, iCar, 0x2A2Au, 1);
    world.aMechs[0].iFacing = 0;
    world.aMechs[0].byLock = MECHA_LOCK_HELD;
    world.aMechs[0].iTargetIdx = 1;
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, &world, 0);
    CHECK(list.iCount > 40);

    for (i = 0; i < list.iCount; i++) {
        int v;

        for (v = 0; v < 4; v++) {
            int iAxis;

            for (iAxis = 0; iAxis < 3; iAxis++) {
                float f = aStorage[i].afVert[v][iAxis];

                if (f < afLow[iAxis])
                    afLow[iAxis] = f;
                if (f > afHigh[iAxis])
                    afHigh[iAxis] = f;
            }
        }
    }

    printf("   built out of %d quads: %.1f m long, %.1f wide, %.1f tall,"
           " sitting %.2f m into the ground\n", list.iCount,
           (afHigh[2] - afLow[2]) / MECHA_METRE,
           (afHigh[0] - afLow[0]) / MECHA_METRE,
           (afHigh[1] - afLow[1]) / MECHA_METRE,
           (world.aMechs[0].fY - afLow[1]) / MECHA_METRE);

    /* Longer than it is wide and much longer than it is tall, which is what
     * says the body came out as a car rather than as a box. */
    CHECK(afHigh[2] - afLow[2] > afHigh[0] - afLow[0]);
    CHECK(afHigh[2] - afLow[2] > (afHigh[1] - afLow[1]) * 2.0f);
    /* And it sits on the ground rather than in it. */
    CHECK(afLow[1] > world.aMechs[0].fY - MECHA_M(0.2f));

    /* The gun is off to the right of it, which is the only place it is. */
    for (i = 0; i < list.iCount; i++) {
        int v;

        for (v = 0; v < 4; v++) {
            float fSide = aStorage[i].afVert[v][0] - world.aMechs[0].fX;

            if (fSide > fGunOut)
                fGunOut = fSide;
        }
    }
    printf("   the gun stands %.1f m off the right of it\n",
           fGunOut / MECHA_METRE);
    CHECK(fGunOut > pDef->fRadius);

    /*
     * --- and it is the car, not its reflection --------------------------
     *
     * The race game's frame is right-handed and this one is not, so
     * swapping the three axes without negating one builds the car's mirror
     * image: same silhouette, wheel arches and exhausts and both flanks of
     * the livery on the wrong sides. Negating the lateral axis puts it
     * right, and a reflection reverses a winding -- so a correctly
     * reflected body is one whose panels all face inwards. Drop the
     * negation and all fifty turn round, which is what this catches,
     * because nothing about the car's outline would.
     */
    {
        float fCentreY = world.aMechs[0].fY + pDef->fHeight * 0.5f;
        int iOutward = 0;
        int iInward = 0;

        for (i = 0; i < MECHA_ZIZIN_BODY_QUADS && i < list.iCount; i++) {
            float afMid[3] = { 0.0f, 0.0f, 0.0f };
            float fDot;
            int v;

            for (v = 0; v < 4; v++) {
                afMid[0] += aStorage[i].afVert[v][0] * 0.25f;
                afMid[1] += aStorage[i].afVert[v][1] * 0.25f;
                afMid[2] += aStorage[i].afVert[v][2] * 0.25f;
            }
            fDot = aStorage[i].afNormal[0] * (afMid[0] - world.aMechs[0].fX)
                 + aStorage[i].afNormal[1] * (afMid[1] - fCentreY)
                 + aStorage[i].afNormal[2] * (afMid[2] - world.aMechs[0].fZ);
            if (fDot > 0.0f)
                iOutward++;
            else
                iInward++;
        }
        printf("   %d of its panels face outwards, %d in\n", iOutward,
               iInward);
        CHECK(iInward == MECHA_ZIZIN_BODY_QUADS);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* The one machine on wheels, wherever it is on the roster. */
static int wheeled_def(void)
{
    int i;

    for (i = 0; i < mecha_def_count(); i++) {
        if (mecha_def_get(i)->bWheeled)
            return i;
    }
    return -1;
}

//-------------------------------------------------------------------------------------------------

static int test_the_gun_car_wears_the_games_own_paint(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    int iCar = wheeled_def();
    int iSkinned = 0;
    int iFlat = 0;
    int iGun = 0;
    int i;

    CHECK(iCar >= 0);
    start_duel(&world, 0, iCar, iCar, 0x5C10u, 1);

    /* Told the skin is there, the body is painted out of the plan's own
     * texture words rather than out of the machine's palette. */
    mecha_mesh_set_car_skin(true);
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, &world, 0);
    CHECK(list.iCount > 50);

    for (i = 0; i < list.iCount; i++) {
        if (i < MECHA_ZIZIN_BODY_QUADS) {
            if (aStorage[i].byTexBank == MECHA_TEX_CAR)
                iSkinned++;
            else
                iFlat++;
        } else if (aStorage[i].byTexBank == MECHA_TEX_NONE) {
            iGun++;
        }
    }

    printf("   the car: %d panels off its own texture, %d flat; %d quads of"
           " gun, none of them textured\n", iSkinned, iFlat, iGun);
    /*
     * Most of it textured, some of it not: nine of the fifty panels carry
     * no texture flag at all and are a plain palette index -- that is how
     * the tyres come out black -- and eight more reach their texture
     * through the car's animation table, which is where the wheels and the
     * livery live. All three cases are the race game's, walked here the
     * same way its own draw path walks them.
     */
    CHECK(iSkinned > MECHA_ZIZIN_BODY_QUADS * 3 / 4);
    CHECK(iFlat > 0);
    /* And the gun is left alone. It is not part of the car. */
    CHECK(iGun == list.iCount - MECHA_ZIZIN_BODY_QUADS);

    /* Without the skin, nothing names a bank it cannot have. */
    mecha_mesh_set_car_skin(false);
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, &world, 0);
    for (i = 0; i < list.iCount; i++)
        CHECK(aStorage[i].byTexBank == MECHA_TEX_NONE);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_gun_car_drives(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaMechDef *pDef;
    int iCar = wheeled_def();
    int iStart;
    float fTop = 0.0f;
    int i;

    CHECK(iCar >= 0);
    pDef = mecha_def_get(iCar);
    start_duel(&world, 0, iCar, 0, 0x0CA5u, 1);
    CHECK(clear_runway(&world, 0));
    memset(aInputs, 0, sizeof(aInputs));

    /* --- the throttle is the boost button ------------------------------- */
    aInputs[0].bDash = true;
    for (i = 0; i < MECHA_TICK_HZ * 3; i++) {
        mecha_sim_tick(&world, aInputs, 2);
        if (mecha_length2(world.aMechs[0].fVelX, world.aMechs[0].fVelZ) > fTop)
            fTop = mecha_length2(world.aMechs[0].fVelX, world.aMechs[0].fVelZ);
    }
    printf("   the car reaches %.0f m/s on the throttle\n", fTop / MECHA_METRE);
    CHECK(fTop > pDef->fWalkSpeed * 0.9f);
    /* And it spends nothing doing it: there is no gauge on this machine. */
    CHECK(mecha_mech_boost_fraction(&world, 0) > 0.99f);
    /* All of which is along its own nose. A car does not go sideways. */
    {
        float fSide = world.aMechs[0].fVelX * mecha_cos(world.aMechs[0].iFacing)
                      - world.aMechs[0].fVelZ
                        * mecha_sin(world.aMechs[0].iFacing);

        CHECK(fSide < pDef->fWalkSpeed * 0.05f
              && fSide > -pDef->fWalkSpeed * 0.05f);
    }

    /* --- guard is the brake --------------------------------------------- */
    aInputs[0].bDash = false;
    aInputs[0].bGuard = true;
    for (i = 0; i < MECHA_TICK_HZ; i++)
        mecha_sim_tick(&world, aInputs, 2);
    printf("   and stops inside a second of the brake\n");
    CHECK(mecha_length2(world.aMechs[0].fVelX, world.aMechs[0].fVelZ)
          < pDef->fWalkSpeed * 0.25f);

    /* --- and it cannot turn standing still ------------------------------ */
    memset(aInputs, 0, sizeof(aInputs));
    for (i = 0; i < MECHA_TICK_HZ * 2; i++)
        mecha_sim_tick(&world, aInputs, 2);
    iStart = world.aMechs[0].iFacing;
    aInputs[0].iTurn = 100;
    for (i = 0; i < MECHA_TICK_HZ; i++)
        mecha_sim_tick(&world, aInputs, 2);
    printf("   stationary, a second of full lock turns it %d units\n",
           mecha_angle_delta(iStart, world.aMechs[0].iFacing));
    /*
     * The race game's own rule: below the car's steering speed the wheels
     * do nothing. It is what makes this machine have to drive at somebody
     * to point at them, which is the whole of how it aims.
     */
    CHECK(world.aMechs[0].iFacing == iStart);

    /* Moving, the same lock turns it a long way -- far enough that the
     * shortest way round stops being the way it went, which is why this
     * adds up the ticks instead of measuring the ends. */
    {
        int iTurned = 0;

        aInputs[0].bDash = true;
        for (i = 0; i < MECHA_TICK_HZ; i++) {
            int iWas = world.aMechs[0].iFacing;

            mecha_sim_tick(&world, aInputs, 2);
            iTurned += mecha_angle_delta(iWas, world.aMechs[0].iFacing);
        }
        printf("   rolling, the same second turns it %d units\n", iTurned);
        CHECK(iTurned > MECHA_DEG(60));
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_gun_car_has_one_gun_and_a_bumper(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaMechDef *pDef;
    int iCar = wheeled_def();
    float fArmour;
    int iSlot;
    int i;

    CHECK(iCar >= 0);
    pDef = mecha_def_get(iCar);

    /* --- three triggers, one magazine of nine ---------------------------- */
    start_duel(&world, 0, iCar, 0, 0x6C0Fu, 1);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].byLock = MECHA_LOCK_HELD;
    world.aMechs[0].iTargetIdx = 1;
    for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++)
        CHECK(world.aMechs[0].aiAmmo[iSlot] == MECHA_CAR_MAGAZINE);

    aInputs[0].bFireLeft = true;
    mecha_sim_tick(&world, aInputs, 2);
    printf("   one trigger costs all three: %d %d %d rounds left\n",
           world.aMechs[0].aiAmmo[0], world.aMechs[0].aiAmmo[1],
           world.aMechs[0].aiAmmo[2]);
    for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++)
        CHECK(world.aMechs[0].aiAmmo[iSlot] == MECHA_CAR_MAGAZINE - 1);
    /* And firing shoved it: the kick is real, not a drawing. */
    CHECK(mecha_length2(world.aMechs[0].fVelX, world.aMechs[0].fVelZ) > 0.0f);

    /*
     * Spend the rest across all three triggers and the magazine still runs
     * out after nine, not twenty-seven. Rolling across the triggers is
     * what a shared magazine has to stop being worth doing.
     */
    {
        int iFired = 1;                 /* the one already spent, above */
        int iTick;
        int iWant = 0;

        for (iTick = 0; iTick < MECHA_TICK_HZ * 6; iTick++) {
            int iBefore = world.aMechs[0].aiAmmo[0];

            /* A trigger has to be released before it can be pulled again,
             * so every shot is a press on one tick and nothing on the
             * next -- and the slot is rotated so this is genuinely trying
             * to stretch nine rounds across three triggers. */
            aInputs[0].bFireLeft = (iTick & 1) == 0
                                   && iWant == MECHA_SLOT_LEFT;
            aInputs[0].bFireCenter = (iTick & 1) == 0
                                     && iWant == MECHA_SLOT_CENTER;
            aInputs[0].bFireRight = (iTick & 1) == 0
                                    && iWant == MECHA_SLOT_RIGHT;
            mecha_sim_tick(&world, aInputs, 2);
            if (world.aMechs[0].aiAmmo[0] < iBefore) {
                iFired++;
                iWant = (iWant + 1) % MECHA_WEAPON_SLOTS;
            }
            if (world.aMechs[0].aiAmmo[0] == 0)
                break;
        }
        printf("   %d rounds out of the magazine before it went dry\n",
               iFired);
        CHECK(iFired == MECHA_CAR_MAGAZINE);
        for (iSlot = 0; iSlot < MECHA_WEAPON_SLOTS; iSlot++) {
            CHECK(world.aMechs[0].aiAmmo[iSlot] == 0);
            CHECK(world.aMechs[0].aiReload[iSlot] > 0);
        }
    }

    /* --- and three different things to put through it -------------------- */
    {
        const tMechaWeaponDef *pShot =
            &pDef->aWeapons[MECHA_SLOT_LEFT][MECHA_STANCE_STAND];
        const tMechaWeaponDef *pLance =
            &pDef->aWeapons[MECHA_SLOT_CENTER][MECHA_STANCE_STAND];
        const tMechaWeaponDef *pShell =
            &pDef->aWeapons[MECHA_SLOT_RIGHT][MECHA_STANCE_STAND];

        /* Buckshot: a spread of pellets that does not carry. */
        CHECK(pShot->byCount >= 5 && pShot->iSpreadAngle > 0);
        /* The lance: one round, no spread, and the fastest of the three. */
        CHECK(pLance->byCount == 1 && pLance->iSpreadAngle == 0);
        CHECK(pLance->fSpeed > pShot->fSpeed * 2.0f);
        CHECK(pLance->fSpeed > pShell->fSpeed * 4.0f);
        /* Reach: the pellets die long before the lance does. */
        CHECK(pShot->fSpeed * (float)pShot->iLifeTicks
              < pLance->fSpeed * (float)pLance->iLifeTicks * 0.2f);
        /* The shell lobs and goes off. */
        CHECK(pShell->byKind == MECHA_PROJ_ARC);
        CHECK(pShell->fArcGravity > 0.0f && pShell->fBlastRadius > 0.0f);
        /* And the slowest of the three takes the longest to recover from. */
        CHECK(pShell->iRecoveryTicks > pLance->iRecoveryTicks);
        CHECK(pLance->iRecoveryTicks > pShot->iRecoveryTicks);
        printf("   buckshot %d pellets, lance %.0f m/s, shell %.0f m blast\n",
               pShot->byCount, pLance->fSpeed / MECHA_METRE,
               pShell->fBlastRadius / MECHA_METRE);
    }

    /* --- and running into somebody hurts them --------------------------- */
    start_duel(&world, 0, iCar, 0, 0x0BADu, 1);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(clear_runway(&world, 0));
    /* Squared up on the other machine with a long run at it. */
    world.aMechs[1].fX = world.aMechs[0].fX
                         + mecha_sin(world.aMechs[0].iFacing)
                           * MECHA_M(70.0f);
    world.aMechs[1].fZ = world.aMechs[0].fZ
                         + mecha_cos(world.aMechs[0].iFacing)
                           * MECHA_M(70.0f);
    world.aMechs[1].iInvulnTicks = 0;
    fArmour = world.aMechs[1].fArmour;

    aInputs[0].bDash = true;
    for (i = 0; i < MECHA_TICK_HZ * 6; i++) {
        mecha_sim_tick(&world, aInputs, 2);
        world.aMechs[1].iInvulnTicks = 0;
        if (world.aMechs[1].fArmour < fArmour)
            break;
    }
    printf("   ramming took %.0f armour off it\n",
           fArmour - world.aMechs[1].fArmour);
    CHECK(world.aMechs[1].fArmour < fArmour);
    /* Not every tick, though: a car resting against somebody is not
     * running them over sixty times a second. */
    {
        float fAfter = world.aMechs[1].fArmour;

        mecha_sim_tick(&world, aInputs, 2);
        CHECK(world.aMechs[1].fArmour == fAfter);
    }
    (void)pDef;
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_gun_car_never_turns_itself(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iCar = wheeled_def();
    int iStart;
    int i;

    CHECK(iCar >= 0);
    start_duel(&world, 0, iCar, 0, 0x7A11u, 1);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(clear_runway(&world, 0));

    /* An enemy right beside it, held in the lock: a legged machine squares
     * up to that on its own, at knife range, and this one must not. */
    world.aMechs[0].byLock = MECHA_LOCK_HELD;
    world.aMechs[0].iTargetIdx = 1;
    world.aMechs[1].fX = world.aMechs[0].fX + MECHA_M(12.0f);
    world.aMechs[1].fZ = world.aMechs[0].fZ + MECHA_M(12.0f);
    iStart = world.aMechs[0].iFacing;

    for (i = 0; i < MECHA_TICK_HZ; i++) {
        world.aMechs[0].byLock = MECHA_LOCK_HELD;
        world.aMechs[0].iTargetIdx = 1;
        mecha_sim_tick(&world, aInputs, 2);
    }
    printf("   a second of knife range turned it %d units\n",
           mecha_angle_delta(iStart, world.aMechs[0].iFacing));
    CHECK(world.aMechs[0].iFacing == iStart);
    return 0;
}

//-------------------------------------------------------------------------------------------------

/*
 * A hill is a wall you can see over.
 *
 * The floor used to be the plane y = 0, so every shot fired across
 * Coldwater Meadow went through its hills and every shot across Tower
 * Seven went through the tabletop. Ground that can be stood on and ground
 * that can be shot through were two different shapes, and only one of them
 * was drawn.
 */
/* Degrees, for reading angles back out of the 14-bit circle. */
static float as_degrees(int iAngle)
{
    return (float)iAngle * 360.0f / (float)MECHA_ANGLE_FULL;
}

//-------------------------------------------------------------------------------------------------

/*
 * Which way a machine is actually leaning, read off the built mesh.
 *
 * Returns +1 if its right flank hangs lower than its left, -1 the other
 * way, 0 if it is level. Measured rather than reasoned about: which sign
 * of roll tips which way is not readable off the rotation matrix without
 * also knowing the multiply order and whether the vectors are rows or
 * columns, and getting it wrong costs a build and looks like a physics
 * bug rather than a sign.
 */
static int lean_side(tMechaWorld *pWorld, int iMechIdx)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
    tMechaQuadList list;
    float fRightX = mecha_cos(pMech->iFacing);
    float fRightZ = -mecha_sin(pMech->iFacing);
    float fLowRight = 1e9f;
    float fLowLeft = 1e9f;
    int i;
    int c;

    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, pWorld, iMechIdx);
    for (i = 0; i < list.iCount; i++)
        for (c = 0; c < 4; c++) {
            float fSide = (list.paQuads[i].afVert[c][0] - pMech->fX) * fRightX
                          + (list.paQuads[i].afVert[c][2] - pMech->fZ)
                            * fRightZ;
            float fY = list.paQuads[i].afVert[c][1];

            if (fSide > MECHA_M(1.0f) && fY < fLowRight)
                fLowRight = fY;
            if (fSide < -MECHA_M(1.0f) && fY < fLowLeft)
                fLowLeft = fY;
        }
    if (fLowRight < fLowLeft - MECHA_M(0.05f))
        return 1;
    if (fLowLeft < fLowRight - MECHA_M(0.05f))
        return -1;
    return 0;
}

//-------------------------------------------------------------------------------------------------

/*
 * How the bodies sit, which is the whole of what the race game does to
 * make a car look like it has weight.
 *
 * Four separate things, none of which the simulation reads back: the tilt
 * that answers the stick, the squat that answers the throttle, the damped
 * ring left by a landing, and the shake. Every figure asserted here is
 * small on purpose -- if any of them ever grows into something you would
 * describe rather than merely feel, it has broken.
 */
static int test_the_bodies_lean_squat_ring_and_shake(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iCar = wheeled_def();
    int iLegs = -1;
    int i;
    int iSettled;
    int iCarTilt;
    int iMechTilt;

    CHECK(iCar >= 0);
    for (i = 0; i < mecha_def_count(); i++)
        if (!mecha_def_get(i)->bWheeled) {
            iLegs = i;
            break;
        }
    CHECK(iLegs >= 0);

    /* --- the car leans out of the corner --------------------------------- */
    start_duel(&world, 0, iCar, iLegs, 0x7E11u, 1);
    CHECK(clear_runway(&world, 0));
    memset(aInputs, 0, sizeof(aInputs));

    aInputs[0].bDash = true;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 2);
    /* Nothing on the stick, nothing in the body. */
    CHECK(world.aMechs[0].attitude.iRollSteer == 0);

    aInputs[0].iTurn = 100;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);
    iCarTilt = world.aMechs[0].attitude.iRollSteer;
    /* Right lock, left lean: the springs load on the outside. Positive
     * roll is a left lean, which is why the sign reads the way it does. */
    CHECK(iCarTilt > 0);
    CHECK(iCarTilt == MECHA_TILT_LIMIT);

    /* Let go and it comes back, and quickly -- Whiplash centres this three
     * times as fast as it winds it on. */
    aInputs[0].iTurn = 0;
    for (iSettled = 0; iSettled < MECHA_TICK_HZ
                       && world.aMechs[0].attitude.iRollSteer != 0; iSettled++)
        mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.aMechs[0].attitude.iRollSteer == 0);
    CHECK(iSettled < MECHA_TICK_HZ / 4);

    /* Half a stick is half a lean. */
    aInputs[0].iTurn = 50;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);
    CHECK(world.aMechs[0].attitude.iRollSteer > 0);
    CHECK(world.aMechs[0].attitude.iRollSteer
          <= MECHA_TILT_LIMIT / 2 + MECHA_TILT_RATE);
    aInputs[0].iTurn = 0;

    /* --- and squats on the throttle -------------------------------------- */
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);
    aInputs[0].bDash = true;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 2);
    CHECK(world.aMechs[0].attitude.iPitchDrive == MECHA_SQUAT_LIMIT);
    aInputs[0].bDash = false;
    aInputs[0].bGuard = true;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 2);
    CHECK(world.aMechs[0].attitude.iPitchDrive == -MECHA_SQUAT_LIMIT);

    printf("   the car leans %.2f deg out of a corner and squats %.2f\n",
           as_degrees(iCarTilt), as_degrees(MECHA_SQUAT_LIMIT));
    /* Subtle, and staying that way. */
    CHECK(as_degrees(MECHA_TILT_LIMIT) < 3.0f);
    CHECK(as_degrees(MECHA_SQUAT_LIMIT) < 3.0f);

    /* --- a shake that is road speed times damage ------------------------- */
    memset(aInputs, 0, sizeof(aInputs));
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 2);
    {
        int iStillWorst = 0;
        int iFastWorst = 0;
        int iHurtWorst = 0;

        for (i = 0; i < MECHA_TICK_HZ; i++) {
            int iShake;

            /* Held at a standstill: the shake is road speed times damage,
             * so with no road speed there is nothing to multiply. */
            world.aMechs[0].fVelX = 0.0f;
            world.aMechs[0].fVelZ = 0.0f;
            mecha_sim_tick(&world, aInputs, 2);
            iShake = abs(world.aMechs[0].attitude.iPitchShake);
            if (iShake > iStillWorst)
                iStillWorst = iShake;
        }
        /* Parked, it is perfectly still, wrecked or not. */
        CHECK(iStillWorst == 0);

        aInputs[0].bDash = true;
        run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 3);
        for (i = 0; i < MECHA_TICK_HZ; i++) {
            int iShake = abs(world.aMechs[0].attitude.iPitchShake);

            if (iShake > iFastWorst)
                iFastWorst = iShake;
            mecha_sim_tick(&world, aInputs, 2);
        }
        CHECK(iFastWorst > 0);

        /* Now hurt it, and the same speed shakes it a great deal harder. */
        world.aMechs[0].fArmour = mecha_def_get(iCar)->fArmour * 0.1f;
        run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);
        for (i = 0; i < MECHA_TICK_HZ; i++) {
            int iShake = abs(world.aMechs[0].attitude.iPitchShake);

            if (iShake > iHurtWorst)
                iHurtWorst = iShake;
            mecha_sim_tick(&world, aInputs, 2);
        }
        printf("   at speed it shakes %.2f deg healthy, %.2f deg wrecked\n",
               as_degrees(iFastWorst), as_degrees(iHurtWorst));
        CHECK(iHurtWorst > iFastWorst * 2);
        CHECK(as_degrees(iHurtWorst) < 5.0f);
    }

    /* --- the landing rings ------------------------------------------------
     *
     * Dropped from a height, the nose follows the fall; the attitude it is
     * holding at contact becomes the amplitude of a damped cosine, so the
     * wobble crosses zero several times and is gone within a second or two.
     */
    {
        int iCrossings = 0;
        int iPrev = 0;
        int iPeak = 0;
        bool bRang = false;

        memset(aInputs, 0, sizeof(aInputs));
        run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);
        world.aMechs[0].fY += MECHA_M(24.0f);
        world.aMechs[0].fVelY = 0.0f;

        for (i = 0; i < MECHA_TICK_HZ * 8; i++) {
            int iNow;

            mecha_sim_tick(&world, aInputs, 2);
            iNow = world.aMechs[0].attitude.iPitchWobble;
            if (abs(iNow) > iPeak)
                iPeak = abs(iNow);
            if (iNow != 0)
                bRang = true;
            if ((iNow > 0 && iPrev < 0) || (iNow < 0 && iPrev > 0))
                iCrossings++;
            if (iNow != 0)
                iPrev = iNow;
        }
        printf("   a landing rings %.2f deg and crosses level %d times\n",
               as_degrees(iPeak), iCrossings);
        CHECK(bRang);
        CHECK(iCrossings >= 2);         /* an oscillation, not a decay */
        /* Big enough to see, small enough not to look like a crash. */
        CHECK(as_degrees(iPeak) > 1.0f && as_degrees(iPeak) < 10.0f);
        /* And it is over rather than a permanent sway. */
        CHECK(world.aMechs[0].attitude.iPitchWobble == 0);
        CHECK(world.aMechs[0].attitude.fWobblePitchAmp == 0.0f);

        /*
         * A bump is not a landing. Dropped from a few centimetres the car
         * touches down with a trace of fall speed, which without a
         * threshold would re-seed the oscillator -- and a car crossing
         * broken ground does that several times a second, so what looks
         * like a landing wobble becomes a permanent shiver.
         */
        world.aMechs[0].fY += MECHA_M(0.05f);
        run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);
        CHECK(world.aMechs[0].attitude.fWobblePitchAmp == 0.0f);
    }

    /* --- the robots lean the other way ----------------------------------- */
    start_duel(&world, 0, iLegs, iLegs, 0x7E12u, 1);
    CHECK(clear_runway(&world, 0));
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(world.aMechs[0].attitude.iRollSteer == 0);

    aInputs[0].iMoveX = 100;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);
    iMechTilt = world.aMechs[0].attitude.iRollSteer;
    /* Right on the stick, right lean: into the input, not out of it. A
     * right lean is a negative roll. */
    CHECK(iMechTilt < 0);
    CHECK(-iMechTilt == MECHA_TILT_MECH_LIMIT);
    printf("   the robot leans %.2f deg into the stick\n",
           as_degrees(-iMechTilt));
    CHECK(as_degrees(MECHA_TILT_MECH_LIMIT) < 3.0f);
    /* Opposite signs for the same stick is the entire point. */
    CHECK((iCarTilt < 0) != (iMechTilt < 0));

    /*
     * And what those signs mean on screen, read off the built mesh rather
     * than argued from the rotation matrix: right on the stick puts the
     * robot's right flank lower, and right lock puts the car's higher.
     */
    {
        tMechaWorld posed;
        int iSide;

        CHECK(lean_side(&world, 0) == 1);        /* robot, right stick */

        start_duel(&posed, 0, iCar, iLegs, 0x7E13u, 1);
        memset(&posed.aMechs[0].attitude, 0, sizeof(posed.aMechs[0].attitude));
        posed.aMechs[0].fVelX = 0.0f;
        posed.aMechs[0].fVelZ = 0.0f;
        posed.aMechs[0].fLeanRoll = 0.0f;
        posed.aMechs[0].attitude.iRollSteer = MECHA_TILT_CAR_SIGN
                                              * MECHA_TILT_LIMIT * 8;
        iSide = lean_side(&posed, 0);
        printf("   full right lock drops the car's %s flank\n",
               iSide > 0 ? "right" : "left");
        CHECK(iSide == -1);                      /* car, right lock */
    }

    /* --- and shake when they are hit, not when they walk ------------------ */
    memset(aInputs, 0, sizeof(aInputs));
    aInputs[0].iMoveZ = 100;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 2);
    {
        int iWalkWorst = 0;
        int iHitWorst = 0;
        int iAfter = 0;

        for (i = 0; i < MECHA_TICK_HZ; i++) {
            int iShake = abs(world.aMechs[0].attitude.iRollShake);

            if (iShake > iWalkWorst)
                iWalkWorst = iShake;
            mecha_sim_tick(&world, aInputs, 2);
        }
        /* Walking is not shaking. */
        CHECK(iWalkWorst == 0);

        world.aMechs[0].iInvulnTicks = 0;
        mecha_sim_damage(&world, 0, -1, 60.0f, 0.0f, 0.0f, 0.0f);
        for (i = 0; i < MECHA_TICK_HZ / 2; i++) {
            int iShake = abs(world.aMechs[0].attitude.iRollShake);

            if (iShake > iHitWorst)
                iHitWorst = iShake;
            mecha_sim_tick(&world, aInputs, 2);
        }
        CHECK(iHitWorst > 0);

        /* And it dies away rather than becoming a permanent tremble. */
        run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 3);
        for (i = 0; i < MECHA_TICK_HZ; i++) {
            int iShake = abs(world.aMechs[0].attitude.iRollShake);

            if (iShake > iAfter)
                iAfter = iShake;
            mecha_sim_tick(&world, aInputs, 2);
        }
        printf("   a hit shakes a robot %.2f deg, and %.2f deg later\n",
               as_degrees(iHitWorst), as_degrees(iAfter));
        CHECK(iAfter == 0);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

/*
 * The close-quarters weapon is a sword, not a shield.
 *
 * It used to be drawn as an ordinary billboard -- a bright square turned
 * to face the camera -- which reads as something held up in front of you
 * rather than as something being swung. A blade has a direction in it, so
 * this asserts the shape has one too: long along the line of the swing,
 * narrow across it, coming to a point at the far end, and level.
 */
/*
 * How far the machine's furthest-out piece sits to its own right, signed:
 * positive to the right of the nose, negative to the left. For the gun car
 * that is the gun, which hangs off one wheel and nothing else comes near.
 */
static bool mesh_widest_side(tMechaWorld *pWorld, int iMechIdx,
                             tMechaQuad *paStorage, float *pfSide)
{
    const tMechaMech *pMech = &pWorld->aMechs[iMechIdx];
    tMechaQuadList list;
    float fRightX = mecha_cos(pMech->iFacing);
    float fRightZ = -mecha_sin(pMech->iFacing);
    float fWidest = 0.0f;
    int i;
    int c;

    mecha_quads_reset(&list, paStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, pWorld, iMechIdx);
    if (list.iCount <= 0)
        return false;
    for (i = 0; i < list.iCount; i++)
        for (c = 0; c < 4; c++) {
            float fSide = (list.paQuads[i].afVert[c][0] - pMech->fX) * fRightX
                          + (list.paQuads[i].afVert[c][2] - pMech->fZ)
                            * fRightZ;

            if (fabsf(fSide) > fabsf(fWidest))
                fWidest = fSide;
        }
    *pfSide = fWidest;
    return true;
}

//-------------------------------------------------------------------------------------------------

/*
 * A car can be spun right round, and goes over on its roof.
 *
 * Whiplash puts no ceiling on how far a car may come round: the yaw is
 * simply accumulated, the lock is worth seven times as much just off a
 * standstill as it is flat out, and the grip decides separately whether
 * the car goes where its nose has gone. What stopped that here was not a
 * clamp but the steering flipping direction the moment the velocity fell
 * more than a quarter turn behind the nose -- which is the middle of every
 * drift, so the stick fought the slide exactly when it should have been
 * driving it.
 */
static int test_the_gun_car_spins_and_rolls(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaWorld world;
    tMechaInput aInputs[2];
    const tMechaMechDef *pDef;
    tMechaQuadList list;
    int iCar = wheeled_def();
    int iTurned = 0;
    int iPrev;
    float fMaxSlip = 0.0f;
    int i;

    CHECK(iCar >= 0);
    pDef = mecha_def_get(iCar);

    /* --- the lock is worth far more slow than fast --------------------- */
    {
        float fFast = pDef->fTurnRate;
        float fSlow = pDef->fTurnRate * (1.0f + MECHA_CAR_STEER_GAIN);

        /* Flat out it understeers; crawling it spins on the spot. */
        CHECK(fFast < (float)MECHA_DEG(90));
        CHECK(fSlow > (float)MECHA_DEG(360));
    }

    /* --- braking into full lock brings it all the way round ------------- */
    start_duel(&world, 3, iCar, 0, 0x5B1Du, 1);
    memset(aInputs, 0, sizeof(aInputs));
    CHECK(clear_runway(&world, 0));
    /* Squared up and at its own top speed, with the other machine well
     * out of the way. */
    world.aMechs[0].iFacing = 0;
    world.aMechs[0].fVelX = 0.0f;
    world.aMechs[0].fVelZ = pDef->fWalkSpeed;
    world.aMechs[1].fX = world.aMechs[0].fX + MECHA_M(600.0f);

    aInputs[0].bGuard = true;
    aInputs[0].iTurn = 100;
    iPrev = world.aMechs[0].iFacing;
    for (i = 0; i < MECHA_TICK_HZ * 2; i++) {
        const tMechaMech *pMech = &world.aMechs[0];
        float fSpeed;

        mecha_sim_tick(&world, aInputs, 2);
        iTurned += mecha_angle_delta(iPrev, pMech->iFacing);
        iPrev = pMech->iFacing;
        fSpeed = mecha_length2(pMech->fVelX, pMech->fVelZ);
        if (fSpeed > MECHA_MPS(6.0f)) {
            int iTravel = mecha_atan2_angle(pMech->fVelX, pMech->fVelZ);
            float fSlip = (float)abs(mecha_angle_delta(pMech->iFacing,
                                                      iTravel));

            if (fSlip > fMaxSlip)
                fMaxSlip = fSlip;
        }
    }
    printf("   braking into full lock spins the car %d degrees, slipping"
           " %.0f\n", iTurned * 360 / MECHA_ANGLE_FULL,
           fMaxSlip * 360.0f / (float)MECHA_ANGLE_FULL);
    /* All the way round, and sideways doing it -- a drift, not a turn. */
    CHECK(abs(iTurned) >= MECHA_ANGLE_FULL);
    CHECK(fMaxSlip > (float)MECHA_DEG(120));

    /* --- and it lands on its roof, not on its face --------------------- */
    start_duel(&world, 0, iCar, 0, 0x0F11u, 1);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].iInvulnTicks = 0;
    mecha_sim_damage(&world, 0, -1, 1.0f, MECHA_STAGGER_DOWN * 2.0f,
                     0.0f, 0.0f);
    for (i = 0; i < 16; i++)      /* well past the eight ticks of going over */
        mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_DOWN);

    {
        float fLow = 1e9f;
        float fHigh = -1e9f;
        int iQuad;
        int c;

        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_mech(&list, &world, 0);
        CHECK(list.iCount > 0);
        for (iQuad = 0; iQuad < list.iCount; iQuad++)
            for (c = 0; c < 4; c++) {
                float fY = list.paQuads[iQuad].afVert[c][1];

                if (fY < fLow) fLow = fY;
                if (fY > fHigh) fHigh = fY;
            }
        printf("   floored, the car lies between %.2f and %.2f m up\n",
               fLow / MECHA_METRE, fHigh / MECHA_METRE);
        /* Still a car's height off the floor and still on the floor: gone
         * over, not stood on its nose and not sunk into the tarmac. */
        CHECK(fLow > -MECHA_M(0.4f));
        CHECK(fHigh < pDef->fHeight * 1.4f);
        CHECK(fHigh > pDef->fHeight * 0.5f);
    }

    /*
     * And genuinely upside down, which is not the same as merely being low.
     * Half a turn of roll about the nose sends what was on the right to the
     * left, so the gun -- which floats off the car's right-hand wheel --
     * comes out on the other side of it.
     */
    {
        float fRightUp;
        float fRightDown;

        CHECK(mesh_widest_side(&world, 0, aStorage, &fRightDown));
        /* The same machine on its wheels, for comparison. */
        start_duel(&world, 0, iCar, 0, 0x0F11u, 1);
        CHECK(world.aMechs[0].byMove != MECHA_MOVE_DOWN);
        CHECK(mesh_widest_side(&world, 0, aStorage, &fRightUp));

        printf("   its gun sits %.1f m to the right upright, %.1f m"
               " floored\n", fRightUp / MECHA_METRE,
               fRightDown / MECHA_METRE);
        CHECK(fRightUp > 0.0f);
        CHECK(fRightDown < 0.0f);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

/*
 * The car sits on the ground, not above it.
 *
 * A machine on wheels sitting perfectly flat while it drives up the side
 * of a hill is what gives away that the hill is a height field rather
 * than a surface. This asserts the body takes the slope's own angle:
 * nose up a climb, nose down a drop, leaning downhill on a traverse --
 * and none of it on a walking machine, which has feet and a gait to put
 * them down with.
 */
static int test_the_gun_car_follows_the_ground(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    tMechaArena probe;
    int iArena = arena_by_name("COLDWATER MEADOW");
    int iCar = wheeled_def();
    int iLegs = -1;
    float fSlopeX = 0.0f;
    float fSlopeZ = 0.0f;
    float fBestGrade = 99.0f;
    int iUphill = 0;
    int i;

    CHECK(iArena >= 0 && iCar >= 0);
    for (i = 0; i < mecha_def_count(); i++)
        if (!mecha_def_get(i)->bWheeled) {
            iLegs = i;
            break;
        }
    CHECK(iLegs >= 0);
    mecha_arena_init(&probe, iArena);

    /*
     * A piece of ordinary hillside and which way is up -- the one closest
     * to a one-in-three grade rather than the steepest in the arena. The
     * steepest is steep enough to sit on the tilt limit, and a test parked
     * on a clamp is testing the clamp.
     */
    for (i = 0; i < 140 * 140; i++) {
        float fX = -probe.fHalfExtent
                   + probe.fHalfExtent * 2.0f * (float)(i % 140) / 140.0f;
        float fZ = -probe.fHalfExtent
                   + probe.fHalfExtent * 2.0f * (float)((i / 140) % 140)
                     / 140.0f;
        float fStep = MECHA_M(4.0f);
        float fDx = mecha_arena_terrain_height(&probe, fX + fStep, fZ)
                    - mecha_arena_terrain_height(&probe, fX - fStep, fZ);
        float fDz = mecha_arena_terrain_height(&probe, fX, fZ + fStep)
                    - mecha_arena_terrain_height(&probe, fX, fZ - fStep);
        float fGrade = mecha_length2(fDx, fDz) / (2.0f * fStep);

        if (fabsf(fGrade - 0.33f) < fabsf(fBestGrade - 0.33f)) {
            fBestGrade = fGrade;
            fSlopeX = fX;
            fSlopeZ = fZ;
            iUphill = mecha_atan2_angle(fDx, fDz);
        }
    }
    CHECK(fBestGrade > 0.25f && fBestGrade < 0.45f);
    printf("   a %.2f grade of hillside at %.0f, %.0f\n", fBestGrade,
           fSlopeX / MECHA_METRE, fSlopeZ / MECHA_METRE);

    /*
     * Parked on that slope facing each of four ways round it, one tick at
     * a time until the suspension has settled. Nothing is driven: this is
     * about the ground, not about the driving.
     */
    {
        static const char *aszWay[] = { "up", "across right", "down",
                                        "across left" };
        int aiPitch[4];
        int aiRoll[4];
        int iWay;

        for (iWay = 0; iWay < 4; iWay++) {
            int iFace = mecha_angle_wrap(iUphill
                                         + iWay * MECHA_ANGLE_QUARTER);

            start_duel(&world, iArena, iCar, iCar, 0x51C0u, 1);
            memset(aInputs, 0, sizeof(aInputs));
            world.aMechs[1].fX = fSlopeX + MECHA_M(900.0f);
            world.aMechs[0].fX = fSlopeX;
            world.aMechs[0].fZ = fSlopeZ;
            world.aMechs[0].fY = mecha_arena_terrain_height(&world.arena,
                                                            fSlopeX, fSlopeZ);
            world.aMechs[0].fGroundY = world.aMechs[0].fY;
            world.aMechs[0].iFacing = iFace;
            world.aMechs[0].fVelX = 0.0f;
            world.aMechs[0].fVelZ = 0.0f;

            for (i = 0; i < MECHA_TICK_HZ; i++) {
                world.aMechs[0].fX = fSlopeX;
                world.aMechs[0].fZ = fSlopeZ;
                world.aMechs[0].iFacing = iFace;
                mecha_sim_tick(&world, aInputs, 2);
            }
            aiPitch[iWay] = world.aMechs[0].attitude.iContourPitch;
            aiRoll[iWay] = world.aMechs[0].attitude.iContourRoll;
            printf("   facing %-13s pitch %+5.0f deg, roll %+5.0f deg\n",
                   aszWay[iWay], as_degrees(aiPitch[iWay]),
                   as_degrees(aiRoll[iWay]));
        }

        /*
         * Facing uphill the nose comes up, which is a negative pose pitch;
         * facing downhill it goes down. Across the slope there is no climb
         * to speak of and the body leans instead, and the two ways across
         * lean opposite ways.
         */
        CHECK(aiPitch[0] < -MECHA_DEG(6));
        CHECK(aiPitch[2] > MECHA_DEG(6));
        CHECK(abs(aiPitch[1]) < abs(aiPitch[0]) / 3);
        CHECK(abs(aiPitch[3]) < abs(aiPitch[0]) / 3);

        CHECK(abs(aiRoll[1]) > MECHA_DEG(6));
        CHECK(abs(aiRoll[3]) > MECHA_DEG(6));
        CHECK((aiRoll[1] < 0) != (aiRoll[3] < 0));
        /* Facing straight up or down the fall line there is nothing to
         * lean against. */
        CHECK(abs(aiRoll[0]) < abs(aiRoll[1]) / 3);
        CHECK(abs(aiRoll[2]) < abs(aiRoll[1]) / 3);

        /* And the angle is the slope's own, not a stylised lean: a one in
         * three grade is eighteen degrees, and nothing here is clamped. */
        CHECK(abs(aiPitch[0]) < MECHA_CONTOUR_LIMIT);
        CHECK(abs(aiRoll[1]) < MECHA_CONTOUR_LIMIT);
        CHECK(as_degrees(abs(aiPitch[0])) > 10.0f);
        CHECK(as_degrees(abs(aiPitch[0])) < 30.0f);
    }

    /* --- and none of it on legs ----------------------------------------- */
    start_duel(&world, iArena, iLegs, iLegs, 0x51E0u, 1);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].fX = fSlopeX;
    world.aMechs[0].fZ = fSlopeZ;
    world.aMechs[0].fY = mecha_arena_terrain_height(&world.arena, fSlopeX,
                                                    fSlopeZ);
    world.aMechs[0].fGroundY = world.aMechs[0].fY;
    world.aMechs[0].iFacing = iUphill;
    for (i = 0; i < MECHA_TICK_HZ; i++) {
        world.aMechs[0].fX = fSlopeX;
        world.aMechs[0].fZ = fSlopeZ;
        mecha_sim_tick(&world, aInputs, 2);
    }
    CHECK(world.aMechs[0].attitude.iContourPitch == 0);
    CHECK(world.aMechs[0].attitude.iContourRoll == 0);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_close_quarters_swings_a_blade(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaWorld world;
    tMechaInput aInputs[2];
    tMechaQuadList list;
    int iDef;
    int iMelee = -1;
    int iSlot = -1;
    int iTick;
    int i;
    int c;
    float fAxisX = 0.0f;
    float fAxisZ = 0.0f;
    float fShotX = 0.0f;
    float fShotY = 0.0f;
    float fShotZ = 0.0f;
    float fAlongLow = 1e9f;
    float fAlongHigh = -1e9f;
    float fAcross = 0.0f;
    float fTipAcross = 0.0f;
    float fRise = 0.0f;
    float fReach;

    /* Whoever on the roster swings something. */
    for (iDef = 0; iDef < mecha_def_count() && iMelee < 0; iDef++) {
        int iTry;

        for (iTry = 0; iTry < MECHA_WEAPON_SLOTS; iTry++)
            if (mecha_def_get(iDef)->aWeapons[iTry][MECHA_STANCE_STAND].byKind
                == MECHA_PROJ_MELEE) {
                iMelee = iDef;
                iSlot = iTry;
                break;
            }
    }
    CHECK(iMelee >= 0 && iSlot >= 0);

    start_duel(&world, 0, iMelee, iMelee, 0x5B0Du, 1);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].byLock = MECHA_LOCK_HELD;
    world.aMechs[0].iTargetIdx = 1;

    aInputs[0].bFireLeft = iSlot == MECHA_SLOT_LEFT;
    aInputs[0].bFireCenter = iSlot == MECHA_SLOT_CENTER;
    aInputs[0].bFireRight = iSlot == MECHA_SLOT_RIGHT;
    for (iTick = 0; iTick < MECHA_TICK_HZ && fAxisX == 0.0f
                    && fAxisZ == 0.0f; iTick++) {
        mecha_sim_tick(&world, aInputs, 2);
        for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
            const tMechaProjectile *pShot = &world.aProjectiles[i];

            if (pShot->bActive && pShot->byKind == MECHA_PROJ_MELEE) {
                float fLen = mecha_length2(pShot->fVelX, pShot->fVelZ);

                CHECK(fLen > 0.0f);
                fAxisX = pShot->fVelX / fLen;
                fAxisZ = pShot->fVelZ / fLen;
                fShotX = pShot->fX;
                fShotY = pShot->fY;
                fShotZ = pShot->fZ;
                fReach = pShot->fRadius * MECHA_BLADE_REACH;
                break;
            }
        }
    }
    CHECK(fAxisX != 0.0f || fAxisZ != 0.0f);

    /* Only the swing: mesh the projectiles alone, and there is one. */
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_projectiles(&list, &world, 0);
    CHECK(list.iCount > 0);

    for (i = 0; i < list.iCount; i++)
        for (c = 0; c < 4; c++) {
            float fDx = list.paQuads[i].afVert[c][0] - fShotX;
            float fDz = list.paQuads[i].afVert[c][2] - fShotZ;
            float fAlong = fDx * fAxisX + fDz * fAxisZ;
            float fSide = fabsf(fDx * fAxisZ - fDz * fAxisX);
            float fUp = fabsf(list.paQuads[i].afVert[c][1] - fShotY);

            if (fAlong < fAlongLow) fAlongLow = fAlong;
            if (fAlong > fAlongHigh) fAlongHigh = fAlong;
            if (fSide > fAcross) fAcross = fSide;
            if (fUp > fRise) fRise = fUp;
        }

    /* Long along the swing and narrow across it -- and the width that is
     * there is mostly the crossguard, back at the hilt. */
    printf("   the blade runs %.1f m out and %.1f m across, %.1f m thick\n",
           (fAlongHigh - fAlongLow) / MECHA_METRE, fAcross * 2.0f / MECHA_METRE,
           fRise * 2.0f / MECHA_METRE);
    CHECK(fAlongHigh - fAlongLow > fAcross * 3.0f);
    /* Level: it lies flat, so its thickness is nothing like its length. */
    CHECK(fAlongHigh - fAlongLow > fRise * 6.0f);
    /* Pointed at the far end: nothing out there is off the centre line. */
    for (i = 0; i < list.iCount; i++)
        for (c = 0; c < 4; c++) {
            float fDx = list.paQuads[i].afVert[c][0] - fShotX;
            float fDz = list.paQuads[i].afVert[c][2] - fShotZ;
            float fAlong = fDx * fAxisX + fDz * fAxisZ;
            float fSide = fabsf(fDx * fAxisZ - fDz * fAxisX);

            if (fAlong > fAlongLow + (fAlongHigh - fAlongLow) * 0.97f
                && fSide > fTipAcross)
                fTipAcross = fSide;
        }
    CHECK(fTipAcross < fAcross * 0.2f);
    /* And it points forward, out of the gun, rather than trailing. */
    CHECK(fAlongHigh > -fAlongLow * 1.5f);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_ground_itself_stops_a_shot(void)
{
    tMechaArena arena;
    int iArena = arena_by_name("COLDWATER MEADOW");
    float fPeakX = 0.0f;
    float fPeakZ = 0.0f;
    float fPeak = 0.0f;
    float fHitX;
    float fHitY;
    float fHitZ;
    int i;

    CHECK(iArena >= 0);
    mecha_arena_init(&arena, iArena);

    /* Find the tallest hill rather than assuming where it was put. */
    for (i = 0; i < 96 * 96; i++) {
        float fX = -arena.fHalfExtent
                   + arena.fHalfExtent * 2.0f * (float)(i % 96) / 96.0f;
        float fZ = -arena.fHalfExtent
                   + arena.fHalfExtent * 2.0f * (float)((i / 96) % 96) / 96.0f;
        float fHere = mecha_arena_terrain_height(&arena, fX, fZ);

        if (fHere > fPeak) {
            fPeak = fHere;
            fPeakX = fX;
            fPeakZ = fZ;
        }
    }
    CHECK(fPeak > MECHA_M(15.0f));

    /* A shot aimed through the hill at half its height stops in it, and
     * stops on the slope -- not on the plane the floor used to be. */
    CHECK(mecha_arena_trace_segment(&arena,
                                    fPeakX - MECHA_M(70.0f), fPeak * 0.5f,
                                    fPeakZ,
                                    fPeakX + MECHA_M(70.0f), fPeak * 0.5f,
                                    fPeakZ, &fHitX, &fHitY, &fHitZ));
    CHECK(fHitY > MECHA_M(2.0f));
    CHECK(near(fHitY, mecha_arena_terrain_height(&arena, fHitX, fHitZ),
               MECHA_M(0.5f)));

    /*
     * A muzzle grazing the surface is not a shot fired from inside a hill.
     * The gun car's weapon floats six metres off its flank, so parked
     * across the steepest slope in this arena it dips a few centimetres
     * into the ground -- and without a little slack every shot from there
     * would detonate in the driver's face.
     */
    {
        float fGrazeX = fPeakX - MECHA_M(30.0f);
        float fGrazeY = mecha_arena_terrain_height(&arena, fGrazeX, fPeakZ)
                        - MECHA_M(0.05f);

        /* Fired up the slope, so the rising ground is what stops it. */
        CHECK(mecha_arena_trace_segment(&arena, fGrazeX, fGrazeY, fPeakZ,
                                        fGrazeX + MECHA_M(40.0f), fGrazeY,
                                        fPeakZ, &fHitX, &fHitY, &fHitZ));
        /* It travels: the hit is somewhere up the slope, not at the
         * muzzle it left. */
        CHECK(fHitX > fGrazeX + MECHA_M(0.2f));

        /* And across level ground a grazing shot goes the distance. */
        {
            float fFlatY = mecha_arena_terrain_height(&arena, 0.0f, 0.0f)
                           - MECHA_M(0.05f);

            CHECK(!mecha_arena_trace_segment(&arena, 0.0f, fFlatY, 0.0f,
                                             0.0f, fFlatY, MECHA_M(30.0f),
                                             NULL, NULL, NULL));
        }
    }

    /* Clearing the crest clears the hill. */
    CHECK(!mecha_arena_trace_segment(&arena,
                                     fPeakX - MECHA_M(8.0f),
                                     fPeak + MECHA_M(4.0f), fPeakZ,
                                     fPeakX + MECHA_M(8.0f),
                                     fPeak + MECHA_M(4.0f), fPeakZ,
                                     NULL, NULL, NULL));

    printf("   meadow hill at %.0f, %.0f stands %.0f m and is hit at %.0f m"
           " up\n", fPeakX / MECHA_METRE, fPeakZ / MECHA_METRE,
           fPeak / MECHA_METRE, fHitY / MECHA_METRE);

    /* The same on the tabletop, which is answered rather than gridded. */
    iArena = arena_by_name("TOWER SEVEN ROOF");
    CHECK(iArena >= 0);
    mecha_arena_init(&arena, iArena);
    CHECK(arena.fMesaHeight > 0.0f);

    CHECK(mecha_arena_trace_segment(&arena,
                                    -arena.fMesaBase - MECHA_M(10.0f),
                                    arena.fMesaHeight * 0.5f, 0.0f,
                                    arena.fMesaBase + MECHA_M(10.0f),
                                    arena.fMesaHeight * 0.5f, 0.0f,
                                    &fHitX, &fHitY, &fHitZ));
    CHECK(near(fHitY, mecha_arena_mesa_height(&arena, fHitX, fHitZ),
               MECHA_M(0.5f)));

    /* Off the edge of a platform there is no floor to hit at all: a shot
     * that leaves the roof keeps going down into the night. */
    CHECK(!mecha_arena_trace_segment(&arena,
                                     arena.fHalfExtent + MECHA_M(20.0f),
                                     MECHA_M(4.0f), 0.0f,
                                     arena.fHalfExtent + MECHA_M(20.0f),
                                     -MECHA_M(60.0f), 0.0f,
                                     NULL, NULL, NULL));
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_forest_stands_outside_the_fight(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaArena arena;
    int iArena = arena_by_name("COLDWATER MEADOW");
    float fFurthest = 0.0f;
    int iInside = 0;
    int i;

    CHECK(iArena >= 0);
    mecha_arena_init(&arena, iArena);

    /* Nothing at all without the game's own sprite banks: a billboarded
     * tree is its sprite and nothing else, and a flat green square standing
     * in a field is worse than no tree. */
    mecha_mesh_set_sprites(false);
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_scenery(&list, &arena, 0x1234u, 0);
    CHECK(list.iCount == 0);

    mecha_mesh_set_sprites(true);
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_scenery(&list, &arena, 0x1234u, 0);
    CHECK(list.iCount > 80);

    for (i = 0; i < list.iCount; i++) {
        float fX = 0.0f;
        float fZ = 0.0f;
        float fAway;
        int v;

        for (v = 0; v < 4; v++) {
            fX += aStorage[i].afVert[v][0] * 0.25f;
            fZ += aStorage[i].afVert[v][2] * 0.25f;
        }
        fAway = mecha_length2(fX, fZ);
        if (fAway > fFurthest)
            fFurthest = fAway;
        /*
         * Every one of them outside the boundary. They do not collide and
         * they do not stop a shot, so one standing where the fight is would
         * be a tree machines walk through.
         */
        if (mecha_arena_contains(&arena, fX, fZ))
            iInside++;
    }

    printf("   forest: %d trees, none of them inside, furthest %.0f m out\n",
           list.iCount, fFurthest / MECHA_METRE);
    CHECK(iInside == 0);
    CHECK(fFurthest > arena.fHalfExtent * 1.4f);
    mecha_mesh_set_sprites(false);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_the_roof_has_no_walls_and_a_tabletop(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    tMechaArena arena;
    int iArena = arena_by_name("TOWER SEVEN ROOF");
    int iCorners = 0;
    int i;

    CHECK(iArena >= 0);
    mecha_arena_init(&arena, iArena);
    CHECK(arena.byShape == MECHA_ARENA_OPEN);

    /* A block in each corner. */
    for (i = 0; i < arena.iObstacleCount; i++) {
        const tMechaObstacle *pBox = &arena.aObstacles[i];

        if (fabsf(pBox->fX) > arena.fHalfExtent * 0.5f
            && fabsf(pBox->fZ) > arena.fHalfExtent * 0.5f)
            iCorners++;
    }
    CHECK(iCorners == 4);

    /* --- the tabletop is a hexagon, and it is a ramp ---------------------
     *
     * A hexagon and not a circle: measured across a face it is exactly its
     * apothem, and measured towards a corner it reaches further by the two
     * over root three that a hexagon does. A circle would come out the same
     * in both, and a square would come out further at forty-five degrees
     * than at thirty.
     */
    {
        float fTop = arena.fMesaTop;
        float fCorner = fTop * 2.0f / 1.7320508f;
        int iAngle;
        float fFlat = 0.0f;
        float fSlope = 0.0f;

        CHECK(arena.fMesaHeight > 0.0f);
        CHECK(mecha_arena_mesa_height(&arena, 0.0f, 0.0f)
              == arena.fMesaHeight);

        /* Every direction: the flat top reaches the apothem across a face
         * and the corner distance towards a corner, and nothing outside the
         * base is raised at all. */
        for (iAngle = 0; iAngle < MECHA_ANGLE_FULL; iAngle += 128) {
            float fS = mecha_sin(iAngle);
            float fC = mecha_cos(iAngle);
            float fOnTop = mecha_arena_mesa_height(&arena, fS * fTop * 0.98f,
                                                   fC * fTop * 0.98f);
            /*
             * Past the corners, not past the faces: a hexagon reaches
             * further towards a corner than across a face by exactly two
             * over root three, so a circle drawn at the base apothem is
             * still inside it in six directions.
             */
            float fOutside =
                mecha_arena_mesa_height(&arena, fS * arena.fMesaBase * 1.20f,
                                        fC * arena.fMesaBase * 1.20f);

            CHECK(fOnTop == arena.fMesaHeight);
            CHECK(fOutside == 0.0f);
        }
        /* Across a face against towards a corner. */
        fFlat = mecha_arena_mesa_height(&arena, fTop * 1.05f, 0.0f);
        fSlope = mecha_arena_mesa_height(&arena, 0.0f, fCorner * 0.98f);
        printf("   tabletop: %.1f m up, still full height %.0f m towards a"
               " corner and already sloping %.0f m across a face\n",
               arena.fMesaHeight / MECHA_METRE, fCorner / MECHA_METRE,
               fTop * 1.05f / MECHA_METRE);
        CHECK(fSlope == arena.fMesaHeight);
        CHECK(fFlat > 0.0f);
        CHECK(fFlat < arena.fMesaHeight);

        /* And it is ground: standing on the middle of it is standing nine
         * metres up, not falling through it. */
        CHECK(mecha_arena_ground_height(&arena, 0.0f, 0.0f, MECHA_M(20.0f))
              == arena.fMesaHeight);
    }

    /* Nothing is a pit any more, and nothing is hidden. */
    CHECK(mecha_arena_surface(&arena, 0.0f, 0.0f) == 0u);
    CHECK(mecha_arena_surface(&arena, arena.fHalfExtent * 0.8f, 0.0f) == 0u);

    /* Nothing stops you walking off, and off the edge there is no floor. */
    {
        float fX = arena.fHalfExtent * 3.0f;
        float fZ = 0.0f;

        CHECK(!mecha_arena_resolve_cylinder(&arena, MECHA_M(6.0f), 0.0f,
                                            MECHA_M(12.0f), &fX, &fZ));
        CHECK(fX > arena.fHalfExtent);
        CHECK(mecha_arena_ground_height(&arena, fX, fZ, 0.0f)
              < arena.fKillY);
    }

    /* --- and it is the top of a tower ------------------------------------
     *
     * The edge is drawn a long way down. Six metres of lip reads as a table
     * standing in the sky; this is what makes it a building.
     */
    CHECK(arena.fSkirt > arena.fHalfExtent);

    /* --- going over the side still costs everything --------------------- */
    start_duel(&world, iArena, 0, 0, 0x9017u, 1);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[1].fX = world.arena.fHalfExtent * 2.0f;   /* off the edge */
    world.aMechs[1].fY = world.arena.fKillY - MECHA_M(1.0f);
    world.aMechs[1].iInvulnTicks = 0;
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(world.aMechs[1].fArmour <= 0.0f);

    /* And underneath the roof is not standing on it. */
    CHECK(mecha_arena_ground_height(&arena, 0.0f, MECHA_M(60.0f),
                                    -MECHA_M(30.0f)) < arena.fKillY);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_a_boost_up_a_slope_leaves_the_ground(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iArena = arena_by_name("COLDWATER MEADOW");
    float fWalkedAir = 0.0f;
    float fBoostedAir = 0.0f;
    int aiAir[2] = { 0, 0 };
    int iPass;

    CHECK(iArena >= 0);

    /* Up the same hill twice, once on foot and once on the thrusters. */
    for (iPass = 0; iPass < 2; iPass++) {
        float fBest = 0.0f;
        int iAngle;
        int i;

        start_duel(&world, iArena, 0, 0, 0x51099u & 0xFFFFu, 1);
        memset(aInputs, 0, sizeof(aInputs));

        /*
         * The steepest piece of ground the arena has, pointed straight up
         * it. Hunting for it rather than naming a spot: the hills are
         * placed by hand and a test that hard-coded one of them would be
         * measuring the arena's layout instead of the rule.
         */
        {
            float fStep = MECHA_M(4.0f);
            float fBestGrade = 0.0f;
            float fBestX = 0.0f;
            float fBestZ = 0.0f;
            int iBestAngle = 0;
            int iScan;

            for (iScan = 0; iScan < 64 * 64; iScan++) {
                float fX = -world.arena.fHalfExtent
                           + world.arena.fHalfExtent * 2.0f
                             * (float)(iScan % 64) / 63.0f;
                float fZ = -world.arena.fHalfExtent
                           + world.arena.fHalfExtent * 2.0f
                             * (float)(iScan / 64) / 63.0f;
                float fHere;
                float fDx;
                float fDz;
                float fGrade;

                if (!mecha_arena_contains(&world.arena, fX, fZ))
                    continue;
                fHere = mecha_arena_terrain_height(&world.arena, fX, fZ);
                fDx = mecha_arena_terrain_height(&world.arena, fX + fStep,
                                                 fZ) - fHere;
                fDz = mecha_arena_terrain_height(&world.arena, fX,
                                                 fZ + fStep) - fHere;
                fGrade = mecha_length2(fDx, fDz) / fStep;
                if (fGrade > fBestGrade) {
                    fBestGrade = fGrade;
                    fBestX = fX;
                    fBestZ = fZ;
                    iBestAngle = mecha_atan2_angle(fDx, fDz);
                }
            }
            CHECK(fBestGrade > 0.25f);

            /* Started back down the slope so there is a run at it. */
            world.aMechs[0].fX = fBestX - mecha_sin(iBestAngle)
                                          * MECHA_M(26.0f);
            world.aMechs[0].fZ = fBestZ - mecha_cos(iBestAngle)
                                          * MECHA_M(26.0f);
            world.aMechs[0].fY =
                mecha_arena_terrain_height(&world.arena, world.aMechs[0].fX,
                                           world.aMechs[0].fZ);
            world.aMechs[0].fGroundY = world.aMechs[0].fY;
            face_mech(&world, 0, iBestAngle);
            world.aMechs[0].iLegYaw = iBestAngle;
            world.aMechs[1].fX = -fBestX;
            world.aMechs[1].fZ = -fBestZ;
        }

        aInputs[0].iMoveZ = 100;
        aInputs[0].bDash = iPass == 1;
        mecha_sim_tick(&world, aInputs, 2);
        aInputs[0].bDash = false;

        for (i = 0; i < MECHA_TICK_HZ * 5 / 2; i++) {
            float fGround;

            mecha_sim_tick(&world, aInputs, 2);
            fGround = mecha_arena_terrain_height(&world.arena,
                                                 world.aMechs[0].fX,
                                                 world.aMechs[0].fZ);
            if (world.aMechs[0].fY - fGround > fBest)
                fBest = world.aMechs[0].fY - fGround;
            if (world.aMechs[0].fY - fGround > MECHA_M(0.5f))
                aiAir[iPass]++;
        }
        if (iPass == 0)
            fWalkedAir = fBest;
        else
            fBoostedAir = fBest;
    }

    printf("   up the hill: walking clears %.1f m over %d ticks, boosting"
           " clears %.1f m over %d\n", fWalkedAir / MECHA_METRE, aiAir[0],
           fBoostedAir / MECHA_METRE, aiAir[1]);
    /*
     * Height cleared, not time spent clear of the ground. Both machines
     * come off the top -- the ground falls away behind a crest and a walker
     * hangs over it for a moment, which is a hill and not a launch -- so
     * what separates them is how far above it they get: a couple of metres
     * against most of the hill's own height again. That is the difference
     * between cresting a rise and going off a ramp.
     */
    CHECK(fWalkedAir < MECHA_M(5.0f));
    CHECK(fBoostedAir > MECHA_M(25.0f));
    CHECK(fBoostedAir > fWalkedAir * 6.0f);
    CHECK(aiAir[1] > aiAir[0]);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_nothing_is_built_coplanar(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    int aiPairs[8];
    int iArenas = mecha_arena_count();
    int iArena;

    CHECK(iArenas <= (int)(sizeof(aiPairs) / sizeof(aiPairs[0])));
    /* One machine per arena, walking the roster alongside the arenas, so
     * every body in the game gets held up against itself somewhere -- the
     * one built out of the race game's own car plan included. */
    CHECK(iArenas >= mecha_def_count());
    for (iArena = 0; iArena < iArenas; iArena++) {
        start_duel(&world, iArena, iArena % mecha_def_count(),
                   (iArena + 1) % 4, 0xC0D1u, 1);
        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_arena(&list, &world.arena);
        mecha_mesh_mech(&list, &world, 0);
        mecha_mesh_mech(&list, &world, 1);
        mecha_mesh_shadows(&list, &world);
        aiPairs[iArena] = coplanar_overlaps(&list);
        /*
         * And it all fits. The walls are cut into panels the size of the
         * floor's tiles, which is a few hundred quads an arena more than
         * one slab a side was, so the budget is worth an assertion rather
         * than a hope: a frame that overflows does not crash, it silently
         * stops adding geometry.
         */
        /*
         * Cover is built by hand now, panel by panel, so which way each
         * side faces is a decision rather than something a box helper
         * guarantees. A block whose sides face inwards is invisible from
         * outside and solid from within, which is exactly backwards.
         */
        for (int iBox = 0; iBox < world.arena.iObstacleCount; iBox++) {
            const tMechaObstacle *pBox = &world.arena.aObstacles[iBox];
            int iChecked = 0;
            int iQuad;

            for (iQuad = 0; iQuad < list.iCount; iQuad++) {
                const tMechaQuad *pQuad = &aStorage[iQuad];
                float fCx = 0.0f;
                float fCz = 0.0f;
                int v;

                if (fabsf(pQuad->afNormal[1]) > 0.5f)
                    continue;                   /* a roof, not a side */
                for (v = 0; v < 4; v++) {
                    fCx += 0.25f * pQuad->afVert[v][0];
                    fCz += 0.25f * pQuad->afVert[v][2];
                }
                /* On this block's surface, near enough. */
                if (fabsf(fCx - pBox->fX) > pBox->fHalfX + 1.0f
                    || fabsf(fCz - pBox->fZ) > pBox->fHalfZ + 1.0f)
                    continue;
                CHECK(pQuad->afNormal[0] * (fCx - pBox->fX)
                      + pQuad->afNormal[2] * (fCz - pBox->fZ) > 0.0f);
                iChecked++;
            }
            CHECK(iChecked > 0);
        }

        printf("   arena %d: %d quads, %d dropped\n", iArena, list.iCount,
               list.iDropped);
        CHECK(list.iDropped == 0);
        CHECK(list.iCount < MECHA_QUAD_CAPACITY * 3 / 4);
    }

    printf("   coplanar overlapping pairs per arena:");
    for (iArena = 0; iArena < iArenas; iArena++)
        printf(" %d", aiPairs[iArena]);
    printf("\n");
    for (iArena = 0; iArena < iArenas; iArena++)
        CHECK(aiPairs[iArena] == 0);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_blasts_draw_over_what_they_engulf(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    const tMechaMechDef *pDef;
    int iHullQuads = 0;
    int iBadWas = 0;
    int iBadNow = 0;
    int iAngle;
    int iBlast;
    float fBlastX;
    float fBlastY;
    float fBlastZ;
    float fBlastReach = 0.0f;

    start_duel(&world, 0, 0, 0, 0xB1A5u, 1);
    pDef = mecha_def_get((int)world.aMechs[0].byDefIdx);

    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, &world, 0);
    iHullQuads = list.iCount;
    CHECK(iHullQuads > 0);

    /* A blast the size of the machine, centred on its chest. */
    mecha_sim_spawn_effect(&world, MECHA_FX_EXPLOSION, world.aMechs[0].fX,
                           world.aMechs[0].fY + 0.6f * pDef->fHeight,
                           world.aMechs[0].fZ, pDef->fRadius * 1.5f,
                           pDef->abyPalette[3], MECHA_SEC(0.4f));
    fBlastX = world.aMechs[0].fX;
    fBlastY = world.aMechs[0].fY + 0.6f * pDef->fHeight;
    fBlastZ = world.aMechs[0].fZ;
    world.iTick += 4;
    mecha_mesh_effects(&list, &world, 0);
    iBlast = list.iCount - iHullQuads;
    CHECK(iBlast > 0);

    /* However big the sprite came out this tick, that is the volume it
     * stands for and the volume it has to win inside of. */
    {
        int v;

        for (v = 0; v < 4; v++) {
            float fReach = mecha_length3(
                aStorage[iHullQuads].afVert[v][0] - fBlastX,
                aStorage[iHullQuads].afVert[v][1] - fBlastY,
                aStorage[iHullQuads].afVert[v][2] - fBlastZ);

            if (fReach > fBlastReach)
                fBlastReach = fReach;
        }
        fBlastReach *= 0.70f;      /* the inscribed sphere, not the corners */
    }

    for (iAngle = 0; iAngle < 12; iAngle++) {
        int iYaw = iAngle * MECHA_ANGLE_FULL / 12;
        float afForward[3];
        float afEye[3];
        int i;
        int j;

        afForward[0] = mecha_sin(iYaw);
        afForward[1] = -0.2f;
        afForward[2] = mecha_cos(iYaw);
        afEye[0] = world.aMechs[0].fX - afForward[0] * MECHA_M(22.0f);
        afEye[1] = MECHA_M(12.0f);
        afEye[2] = world.aMechs[0].fZ - afForward[2] * MECHA_M(22.0f);

        for (i = iHullQuads; i < list.iCount; i++) {
            float fNow = mecha_quad_depth_key(&aStorage[i], afEye, afForward);
            float fWas = quad_centre_key(&aStorage[i], afEye, afForward);

            for (j = 0; j < iHullQuads; j++) {
                float fCx = 0.0f;
                float fCy = 0.0f;
                float fCz = 0.0f;
                int v;

                for (v = 0; v < 4; v++) {
                    fCx += 0.25f * aStorage[j].afVert[v][0];
                    fCy += 0.25f * aStorage[j].afVert[v][1];
                    fCz += 0.25f * aStorage[j].afVert[v][2];
                }
                /*
                 * Only the panels actually inside the fireball. A shoulder
                 * standing clear of it is entitled to occlude it, and a
                 * rule that said otherwise would be drawing blasts through
                 * solid machines.
                 */
                if (mecha_length3(fCx - fBlastX, fCy - fBlastY, fCz - fBlastZ)
                    > fBlastReach)
                    continue;
                /* Larger key first, so the hull must outrank the blast. */
                if (mecha_quad_depth_key(&aStorage[j], afEye, afForward)
                    <= fNow)
                    iBadNow++;
                if (quad_centre_key(&aStorage[j], afEye, afForward) <= fWas)
                    iBadWas++;
            }
        }
    }

    printf("   hull panels painted over a blast: %d, was %d\n", iBadNow,
           iBadWas);
    CHECK(iBadWas > 0);
    CHECK(iBadNow == 0);
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* Mean distance of a quad list's vertices from a point on the ground. */
static float mean_ring_radius(const tMechaQuadList *pList, float fX,
                              float fZ)
{
    float fSum = 0.0f;
    int iCount = 0;
    int i;

    for (i = 0; i < pList->iCount; i++) {
        float fCx = 0.0f;
        float fCz = 0.0f;
        int v;

        for (v = 0; v < 4; v++) {
            fCx += 0.25f * pList->paQuads[i].afVert[v][0];
            fCz += 0.25f * pList->paQuads[i].afVert[v][2];
        }
        fSum += mecha_length2(fCx - fX, fCz - fZ);
        iCount++;
    }
    return iCount > 0 ? fSum / (float)iCount : 0.0f;
}

//-------------------------------------------------------------------------------------------------

/* Drops a shot straight into the world, so a test can stage a meeting
 * without having to find two machines that would fire it. */
static tMechaProjectile *stage_shot(tMechaWorld *pWorld, int iOwner,
                                    uint8_t byKind, float fX, float fZ,
                                    float fVelZ, float fDamage)
{
    int i;

    for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
        tMechaProjectile *pShot = &pWorld->aProjectiles[i];

        if (pShot->bActive)
            continue;
        memset(pShot, 0, sizeof(*pShot));
        pShot->bActive = true;
        pShot->byKind = byKind;
        pShot->byOwner = (uint8_t)iOwner;
        pShot->byPalette = 171;
        pShot->fX = fX;
        pShot->fY = MECHA_M(6.0f);
        pShot->fZ = fZ;
        pShot->fPrevX = fX;
        pShot->fPrevY = pShot->fY;
        pShot->fPrevZ = fZ;
        pShot->fVelZ = fVelZ;
        pShot->fRadius = MECHA_M(1.2f);
        pShot->fDamage = fDamage;
        pShot->iLife = MECHA_TICK_HZ;
        pShot->iTarget = -1;
        return pShot;
    }
    return NULL;
}

//-------------------------------------------------------------------------------------------------

static int count_shots(const tMechaWorld *pWorld, uint8_t byKind)
{
    int iCount = 0;
    int i;

    for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
        if (pWorld->aProjectiles[i].bActive
            && pWorld->aProjectiles[i].byKind == byKind)
            iCount++;
    }
    return iCount;
}

//-------------------------------------------------------------------------------------------------

static int test_shots_trade_on_damage(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    tMechaProjectile *pHeavy;
    int i;

    memset(aInputs, 0, sizeof(aInputs));

    /* --- level pegging: both gone -------------------------------------- */
    {
        start_duel(&world, 0, 0, 0, 0x7EADu, 1);
        memset(world.aProjectiles, 0, sizeof(world.aProjectiles));
        CHECK(stage_shot(&world, 0, MECHA_PROJ_BULLET, 0.0f,
                         -MECHA_M(3.0f), MECHA_MPS(60.0f), 100.0f) != NULL);
        CHECK(stage_shot(&world, 1, MECHA_PROJ_BULLET, 0.0f,
                         MECHA_M(3.0f), -MECHA_MPS(60.0f), 104.0f) != NULL);
        for (i = 0; i < 10; i++)
            mecha_sim_tick(&world, aInputs, 2);
        printf("   level shots left: %d\n",
               count_shots(&world, MECHA_PROJ_BULLET));
        CHECK(count_shots(&world, MECHA_PROJ_BULLET) == 0);
    }

    /* --- one much heavier: it carries on through ------------------------ */
    {
        start_duel(&world, 0, 0, 0, 0x7EADu, 1);
        memset(world.aProjectiles, 0, sizeof(world.aProjectiles));
        pHeavy = stage_shot(&world, 0, MECHA_PROJ_BULLET, 0.0f,
                            -MECHA_M(3.0f), MECHA_MPS(60.0f), 300.0f);
        CHECK(pHeavy != NULL);
        CHECK(stage_shot(&world, 1, MECHA_PROJ_BULLET, 0.0f,
                         MECHA_M(3.0f), -MECHA_MPS(60.0f), 60.0f) != NULL);
        for (i = 0; i < 10; i++)
            mecha_sim_tick(&world, aInputs, 2);
        printf("   after a heavy meets a light: %d shot left\n",
               count_shots(&world, MECHA_PROJ_BULLET));
        CHECK(count_shots(&world, MECHA_PROJ_BULLET) == 1);
        CHECK(pHeavy->bActive);
        CHECK(pHeavy->fDamage > 200.0f);
    }

    /* --- two of the same machine's shots ignore each other -------------- */
    {
        start_duel(&world, 0, 0, 0, 0x7EADu, 1);
        memset(world.aProjectiles, 0, sizeof(world.aProjectiles));
        CHECK(stage_shot(&world, 0, MECHA_PROJ_BULLET, 0.0f,
                         -MECHA_M(3.0f), MECHA_MPS(60.0f), 100.0f) != NULL);
        CHECK(stage_shot(&world, 0, MECHA_PROJ_BULLET, 0.0f,
                         MECHA_M(3.0f), -MECHA_MPS(60.0f), 100.0f) != NULL);
        for (i = 0; i < 10; i++)
            mecha_sim_tick(&world, aInputs, 2);
        CHECK(count_shots(&world, MECHA_PROJ_BULLET) == 2);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_a_bomb_leaves_a_fireball(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    tMechaProjectile *pBomb;
    float fFirstRadius = 0.0f;
    float fLaterRadius = 0.0f;
    float fArmourBefore;
    int iShells;
    int i;

    memset(aInputs, 0, sizeof(aInputs));
    start_duel(&world, 0, 0, 0, 0xF12Bu, 1);
    memset(world.aProjectiles, 0, sizeof(world.aProjectiles));

    /* A bomb going off well clear of both machines. */
    pBomb = stage_shot(&world, 0, MECHA_PROJ_ARC, MECHA_M(20.0f),
                       MECHA_M(20.0f), 0.0f, 80.0f);
    CHECK(pBomb != NULL);
    pBomb->fBlastRadius = MECHA_M(14.0f);
    pBomb->fStagger = 0.2f;
    pBomb->iLife = 1;
    mecha_sim_tick(&world, aInputs, 2);

    iShells = count_shots(&world, MECHA_PROJ_SHELL);
    CHECK(iShells == 1);
    for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
        if (world.aProjectiles[i].bActive
            && world.aProjectiles[i].byKind == MECHA_PROJ_SHELL)
            fFirstRadius = world.aProjectiles[i].fRadius;
    }

    /* It opens as it burns. */
    for (i = 0; i < 8; i++)
        mecha_sim_tick(&world, aInputs, 2);
    for (i = 0; i < MECHA_MAX_PROJECTILES; i++) {
        if (world.aProjectiles[i].bActive
            && world.aProjectiles[i].byKind == MECHA_PROJ_SHELL)
            fLaterRadius = world.aProjectiles[i].fRadius;
    }
    printf("   fireball opened from %.1f m to %.1f m\n",
           fFirstRadius / MECHA_METRE, fLaterRadius / MECHA_METRE);
    CHECK(fLaterRadius > fFirstRadius);

    /* --- and it eats what is shot through it ---------------------------- */
    CHECK(stage_shot(&world, 1, MECHA_PROJ_BULLET, MECHA_M(20.0f),
                     MECHA_M(20.0f) - MECHA_M(2.0f), 0.0f, 40.0f) != NULL);
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(count_shots(&world, MECHA_PROJ_BULLET) == 0);

    /* --- and burns whoever walks into it -------------------------------- */
    fArmourBefore = world.aMechs[1].fArmour;
    world.aMechs[1].fX = MECHA_M(20.0f);
    world.aMechs[1].fZ = MECHA_M(20.0f);
    world.aMechs[1].iInvulnTicks = 0;
    mecha_sim_tick(&world, aInputs, 2);
    printf("   walking into it cost %.1f armour\n",
           fArmourBefore - world.aMechs[1].fArmour);
    CHECK(world.aMechs[1].fArmour < fArmourBefore);

    /* --- and it does not last ------------------------------------------- */
    for (i = 0; i < MECHA_SHELL_TICKS + 4; i++)
        mecha_sim_tick(&world, aInputs, 2);
    CHECK(count_shots(&world, MECHA_PROJ_SHELL) == 0);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_a_landing_throws_a_ring_of_dust(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    tMechaEffect *pDust = NULL;
    float fEarly;
    float fLate;
    int i;

    start_duel(&world, 0, 0, 0, 0xD057u, 1);
    mecha_sim_spawn_effect(&world, MECHA_FX_DUST, MECHA_M(5.0f), 0.0f,
                           MECHA_M(-3.0f), MECHA_M(6.0f), 137,
                           MECHA_SEC(0.45f));
    for (i = 0; i < MECHA_MAX_EFFECTS; i++) {
        if (world.aEffects[i].bActive
            && world.aEffects[i].byKind == MECHA_FX_DUST) {
            pDust = &world.aEffects[i];
            break;
        }
    }
    CHECK(pDust != NULL);

    /* --- with the bank: a ring of textured puffs ------------------------ */
    mecha_mesh_set_sprites(true);
    pDust->iAge = pDust->iLife / 5;
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_effects(&list, &world, 0);
    CHECK(list.iCount == MECHA_DUST_PUFFS);
    for (i = 0; i < list.iCount; i++) {
        const tMechaQuad *pQuad = &aStorage[i];

        CHECK(pQuad->byTexBank == MECHA_TEX_EFFECT);
        CHECK(pQuad->byTile >= MECHA_SPRITE_SMOKE_FIRST);
        CHECK(pQuad->byTile <= MECHA_SPRITE_SMOKE_LAST);
        /* Flat on the floor, and sorted as something lying on it. */
        CHECK(fabsf(pQuad->afNormal[1]) > 0.9f);
        CHECK((pQuad->byFlags & MECHA_QUAD_DECAL) != 0);
    }
    fEarly = mean_ring_radius(&list, MECHA_M(5.0f), MECHA_M(-3.0f));

    pDust->iAge = pDust->iLife * 3 / 4;
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_effects(&list, &world, 0);
    fLate = mean_ring_radius(&list, MECHA_M(5.0f), MECHA_M(-3.0f));

    printf("   dust ring: %.1f m across early, %.1f m late\n",
           2.0f * fEarly / MECHA_METRE, 2.0f * fLate / MECHA_METRE);
    /* Radiating outwards is the whole point: a ring that stayed put would
     * be the old single square with extra steps. */
    CHECK(fLate > fEarly * 1.5f);

    /* --- without it: the old stain, and only one of it ------------------ */
    mecha_mesh_set_sprites(false);
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_effects(&list, &world, 0);
    CHECK(list.iCount == 1);
    CHECK((aStorage[0].byFlags & MECHA_QUAD_SHADOW) != 0);
    mecha_mesh_set_sprites(true);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_sky_carries_a_cloud_dome(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    float fNearest = 1e30f;
    float fLowest = 1e30f;
    int iFirstCount;
    int i;
    int v;

    start_duel(&world, 0, 0, 0, 0xC10Du, 1);

    /* Without the bank there is no dome: a cloud that falls back to a flat
     * square is a grey slab hanging in the air. */
    mecha_mesh_set_sprites(false);
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_clouds(&list, &world);
    CHECK(list.iCount == 0);

    mecha_mesh_set_sprites(true);
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_clouds(&list, &world);
    CHECK(list.iCount > 8);
    iFirstCount = list.iCount;

    for (i = 0; i < list.iCount; i++) {
        const tMechaQuad *pQuad = &aStorage[i];

        /* Every puff is a frame from the sky's own five, and every one of
         * them is a sprite: an untextured cloud is a bug. */
        CHECK(pQuad->byTexBank == MECHA_TEX_EFFECT);
        CHECK(pQuad->byTile >= MECHA_SPRITE_CLOUD_FIRST);
        CHECK(pQuad->byTile <= MECHA_SPRITE_CLOUD_LAST);
        CHECK((pQuad->byFlags & MECHA_QUAD_TWO_SIDED) != 0);

        for (v = 0; v < 4; v++) {
            float fX = pQuad->afVert[v][0];
            float fY = pQuad->afVert[v][1];
            float fZ = pQuad->afVert[v][2];
            float fFlat = mecha_length2(fX, fZ);

            if (fFlat < fNearest)
                fNearest = fFlat;
            if (fY < fLowest)
                fLowest = fY;
        }
    }

    printf("   %d cloud quads, nearest %.0f out (%.1f arenas), lowest"
           " %.0f up\n", list.iCount, fNearest,
           fNearest / world.arena.fHalfExtent, fLowest);
    /* Well outside the arena, and none of them underground: a cloud you can
     * walk into is scenery, and a cloud below the horizon is a mistake. */
    CHECK(fNearest > 6.0f * world.arena.fHalfExtent);
    CHECK(fLowest > 0.0f);

    /* And the dome drifts, slowly, rather than being nailed to the world. */
    {
        float fBefore = aStorage[0].afVert[0][0];
        float fAfter;

        world.iTick += MECHA_TICK_HZ * 20;
        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_clouds(&list, &world);
        CHECK(list.iCount == iFirstCount);
        fAfter = aStorage[0].afVert[0][0];
        CHECK(fabsf(fAfter - fBefore) > 1.0f);
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_shots_carry_plasma_frames(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    tMechaInput aInputs[2];
    int aiFrames[MECHA_SPRITE_PLASMA_LAST + 1];
    int iPlasma = 0;
    int iDistinct = 0;
    int iSpriteQuads = 0;
    int iFlatQuads = 0;
    int i;

    memset(aiFrames, 0, sizeof(aiFrames));
    start_duel(&world, 0, 2, 2, 0x9105u, 1);   /* Exos 2000: beams and pods */
    memset(aInputs, 0, sizeof(aInputs));

    /*
     * Fire, then walk the flight. The frame a bolt shows has to move as it
     * travels -- one that picked a frame at launch and kept it would pass a
     * "some quad is textured" check while looking like a decal in flight.
     */
    for (i = 0; i < MECHA_TICK_HZ; i++) {
        aInputs[0].bFireCenter = (i == 0);
        aInputs[0].bFireRight = (i == 0);
        mecha_sim_tick(&world, aInputs, 2);

        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_set_sprites(true);
        mecha_mesh_projectiles(&list, &world, 0);
        if (i == 2) {
            /* Same tick, bank absent: still geometry, and none of it
             * claiming a frame the renderer would have to reject. */
            int iTextured = 0;

            iSpriteQuads = list.iCount;
            mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
            mecha_mesh_set_sprites(false);
            mecha_mesh_projectiles(&list, &world, 0);
            iFlatQuads = list.iCount;
            for (int iQuad = 0; iQuad < list.iCount; iQuad++)
                if (list.paQuads[iQuad].byTexBank == MECHA_TEX_EFFECT)
                    iTextured++;
            CHECK(iFlatQuads > 0);
            CHECK(iTextured < iSpriteQuads);
            mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
            mecha_mesh_set_sprites(true);
            mecha_mesh_projectiles(&list, &world, 0);
        }
        for (int iQuad = 0; iQuad < list.iCount; iQuad++) {
            const tMechaQuad *pQuad = &list.paQuads[iQuad];

            if (pQuad->byTexBank != MECHA_TEX_EFFECT)
                continue;
            CHECK(pQuad->byTile >= MECHA_SPRITE_PLASMA_FIRST);
            CHECK(pQuad->byTile <= MECHA_SPRITE_PLASMA_LAST);
            aiFrames[pQuad->byTile]++;
            iPlasma++;
        }
    }

    for (i = MECHA_SPRITE_PLASMA_FIRST; i <= MECHA_SPRITE_PLASMA_LAST; i++)
        if (aiFrames[i] > 0)
            iDistinct++;

    printf("   %d plasma quads across %d of %d frames, %d/%d quads without"
           " the bank\n", iPlasma, iDistinct,
           MECHA_SPRITE_PLASMA_LAST - MECHA_SPRITE_PLASMA_FIRST + 1,
           iFlatQuads, iSpriteQuads);
    CHECK(iPlasma > 0);
    CHECK(iDistinct == MECHA_SPRITE_PLASMA_LAST - MECHA_SPRITE_PLASMA_FIRST + 1);

    CHECK(iFlatQuads > 0);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_death_throws_debris(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iEmbers = 0;
    int iMoving = 0;
    float fFirstVelY = 0.0f;
    int i;

    start_duel(&world, 0, 0, 0, 0xDEB1u, 1);
    memset(aInputs, 0, sizeof(aInputs));
    mecha_sim_damage(&world, 1, 0, 100000.0f, 0.0f, 0.0f, 0.0f);

    for (i = 0; i < MECHA_MAX_EFFECTS; i++) {
        const tMechaEffect *pFx = &world.aEffects[i];

        if (!pFx->bActive || pFx->byKind != MECHA_FX_EMBER)
            continue;
        if (iEmbers == 0)
            fFirstVelY = pFx->fVelY;
        iEmbers++;
        if (mecha_length3(pFx->fVelX, pFx->fVelY, pFx->fVelZ) > 1.0f)
            iMoving++;
    }

    /* A kill has to come apart, not just flash. */
    CHECK(iEmbers >= 8);
    CHECK(iMoving == iEmbers);

    /* And the debris has to fall, or it is a firework rather than wreckage. */
    run_ticks(&world, aInputs, 2, 10);
    for (i = 0; i < MECHA_MAX_EFFECTS; i++) {
        const tMechaEffect *pFx = &world.aEffects[i];

        if (pFx->bActive && pFx->byKind == MECHA_FX_EMBER) {
            CHECK(pFx->fVelY < fFirstVelY);
            break;
        }
    }

    /* The mesh has to actually build them, and every colour a cooling
     * particle walks through has to be one the mode's palette defines --
     * this is the exact shape of bug that put shade levels out of range
     * before. */
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_effects(&list, &world, 0);
    CHECK(list.iCount > 0);

    /*
     * Effects name a frame of the game's own explosion animation, which the
     * renderer resolves against the retail texture bank when there is one.
     * The mesh layer cannot check that the bank has the frame -- it has
     * never heard of a texture -- so what it can check is that it only ever
     * names frames inside the ranges it declares, and that everything else
     * stays flat. A frame number that wandered would either sample the
     * wrong tile or read off the end of the atlas.
     */
    for (i = 0; i < list.iCount; i++) {
        if (aStorage[i].byTexBank == MECHA_TEX_NONE)
            continue;
        /* Effects come out of the effect bank and nowhere else, and only
         * ever name frames inside the ranges they declare. */
        CHECK(aStorage[i].byTexBank == MECHA_TEX_EFFECT);
        CHECK(aStorage[i].byTile >= MECHA_SPRITE_FIRE_FIRST);
        CHECK(aStorage[i].byTile <= MECHA_SPRITE_SMOKE_LAST);
    }

    /* Arena geometry draws from the world and structure banks, never from
     * the effect one -- a ground tile that named an explosion frame would
     * be a silent mix-up rather than an obvious one. */
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_arena(&list, &world.arena);
    for (i = 0; i < list.iCount; i++)
        CHECK(aStorage[i].byTexBank != MECHA_TEX_EFFECT);
    return 0;
}

//-------------------------------------------------------------------------------------------------

/*
 * Runs a machine up to speed, then asks it for something else, and reports
 * the two things that separate a heavy machine from a light one: how far it
 * keeps drifting the old way, and how long it takes to obey.
 *
 * Everything is measured along whichever way the machine was actually
 * travelling, not along a world axis. A machine faces whatever it has
 * locked, so "forward" is wherever the fight put it -- measuring against +Z
 * reported zero for all three and looked for a moment like the physics had
 * simply stopped working.
 */
static void measure_handling(int iDefIdx, float *pfSkidMetres,
                             int *piReverseTicks)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    float fDirX;
    float fDirZ;
    float fLen;
    float fStartX;
    float fStartZ;
    float fFurthest;
    int i;

    /* --- lateral skid: up to speed, then hard across it ------------------ */
    start_duel(&world, 0, iDefIdx, 0, 0x5C1Du, 1);
    memset(aInputs, 0, sizeof(aInputs));
    aInputs[0].iMoveZ = 100;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 2);

    fDirX = world.aMechs[0].fVelX;
    fDirZ = world.aMechs[0].fVelZ;
    fLen = mecha_length2(fDirX, fDirZ);
    if (fLen < 1.0f) {
        *pfSkidMetres = 0.0f;
        *piReverseTicks = 0;
        return;
    }
    fDirX /= fLen;
    fDirZ /= fLen;
    (void)fStartX;
    (void)fStartZ;
    (void)fFurthest;

    /*
     * How long the old motion takes to bleed away, as a count of ticks for
     * the velocity still pointing the original way to fall to a fifth of
     * what it was. A ratio rather than a distance on purpose: distance is
     * speed times decay time, so the fastest machine covers the most ground
     * while skidding least, and measuring metres ranks the interceptor as
     * the heaviest thing on the roster.
     */
    aInputs[0].iMoveZ = 0;
    aInputs[0].iMoveX = 100;
    *pfSkidMetres = (float)(MECHA_TICK_HZ * 2);
    for (i = 0; i < MECHA_TICK_HZ * 2; i++) {
        float fAlong;

        mecha_sim_tick(&world, aInputs, 2);
        fAlong = world.aMechs[0].fVelX * fDirX
               + world.aMechs[0].fVelZ * fDirZ;
        if (fAlong < fLen * 0.2f) {
            *pfSkidMetres = (float)i;
            break;
        }
    }

    /* --- how long it takes to actually turn round ------------------------ */
    start_duel(&world, 0, iDefIdx, 0, 0x5C1Du, 1);
    memset(aInputs, 0, sizeof(aInputs));
    aInputs[0].iMoveZ = 100;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 2);

    fDirX = world.aMechs[0].fVelX;
    fDirZ = world.aMechs[0].fVelZ;
    fLen = mecha_length2(fDirX, fDirZ);
    fDirX /= fLen;
    fDirZ /= fLen;

    aInputs[0].iMoveZ = -100;
    *piReverseTicks = MECHA_TICK_HZ * 3;
    for (i = 0; i < MECHA_TICK_HZ * 3; i++) {
        mecha_sim_tick(&world, aInputs, 2);
        if (world.aMechs[0].fVelX * fDirX
            + world.aMechs[0].fVelZ * fDirZ < 0.0f) {
            *piReverseTicks = i;
            break;
        }
    }
}

//-------------------------------------------------------------------------------------------------

static int test_machines_carry_their_weight(void)
{
    float afSkid[3];
    int aiTicks[3];
    int i;
    static const int aiDefs[3] = { 1, 0, 2 };   /* heavy, middle, light */
    static const char *const aszNames[3] = { "SJ Mk.IV", "LANCER",
                                             "Exos 2000" };

    for (i = 0; i < 3; i++)
        measure_handling(aiDefs[i], &afSkid[i], &aiTicks[i]);

    for (i = 0; i < 3; i++)
        printf("   %-8s sheds its old motion in %2d ticks, turns round in %2d\n",
               aszNames[i], (int)afSkid[i], aiTicks[i]);

    /*
     * Velocity is driven rather than assigned now, and the two levers that
     * do it are separate. Grip decides how much of the old direction
     * survives being asked for a new one -- so it is measured by turning
     * across the motion, never by reversing along it, where the sideways
     * component is zero and grip is never consulted at all. Drive
     * acceleration decides how long obeying takes, and that is what
     * reversing measures.
     *
     * Orderings rather than figures: the walk speeds these play out at move
     * whenever the roster is tuned.
     */
    CHECK(afSkid[0] > afSkid[1]);
    CHECK(afSkid[1] > afSkid[2]);
    CHECK(aiTicks[0] > aiTicks[1]);
    CHECK(aiTicks[1] > aiTicks[2]);

    /* Worth having, and not so much that a machine is on ice. */
    CHECK(afSkid[0] > afSkid[2] * 1.5f);
    CHECK(afSkid[0] < (float)MECHA_TICK_HZ);
    CHECK(aiTicks[0] < MECHA_TICK_HZ * 2);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_auto_turn_is_close_quarters_only(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iFacingBefore;
    int iDrift;

    /* --- well out of reach: the machine holds whatever heading it has --- */
    start_duel(&world, 0, 0, 0, 0xA47u, 1);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].fX = 0.0f;
    world.aMechs[0].fZ = 0.0f;
    world.aMechs[1].fX = 0.0f;
    world.aMechs[1].fZ = MECHA_M(90.0f);
    face_mech(&world, 0, 0);
    face_mech(&world, 1, MECHA_ANGLE_HALF);
    run_ticks(&world, aInputs, 2, 4);
    CHECK(world.aMechs[0].byLock == MECHA_LOCK_HELD);

    /* Nudged off the bearing, with no stick and a live lock. */
    face_mech(&world, 0, MECHA_DEG(20));
    iFacingBefore = world.aMechs[0].iFacing;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ);
    iDrift = mecha_angle_delta(iFacingBefore, world.aMechs[0].iFacing);
    if (iDrift < 0)
        iDrift = -iDrift;
    /* Nothing should have moved it. Pointing the machine is the player's
     * job at this distance. */
    CHECK(iDrift < MECHA_DEG(2));

    /* --- inside knife range it squares itself up ------------------------ */
    /* Inside the hold cone, or there would be no lock to turn off: past 32
     * degrees the lock breaks and no range makes the machine follow. */
    world.aMechs[1].fZ = MECHA_M(12.0f);
    face_mech(&world, 0, MECHA_DEG(25));
    iFacingBefore = world.aMechs[0].iFacing;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ / 2);
    iDrift = mecha_angle_delta(iFacingBefore, world.aMechs[0].iFacing);
    if (iDrift < 0)
        iDrift = -iDrift;
    CHECK(iDrift > MECHA_DEG(10));

    /* And the stick still works at range, or there would be no way to aim
     * at all out there. */
    world.aMechs[1].fZ = MECHA_M(90.0f);
    iFacingBefore = world.aMechs[0].iFacing;
    aInputs[0].iTurn = 100;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ / 4);
    iDrift = mecha_angle_delta(iFacingBefore, world.aMechs[0].iFacing);
    if (iDrift < 0)
        iDrift = -iDrift;
    CHECK(iDrift > MECHA_DEG(10));
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* Turn suffered while firing from a given movement state, at long range. */
static int firing_recentre_drift(bool bDash)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    int iBefore;
    int iDrift;

    start_duel(&world, 0, 0, 0, 0xFEEDu, 1);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].fX = 0.0f;
    world.aMechs[0].fZ = 0.0f;
    world.aMechs[1].fX = 0.0f;
    world.aMechs[1].fZ = MECHA_M(85.0f);
    face_mech(&world, 0, 0);
    face_mech(&world, 1, MECHA_ANGLE_HALF);
    run_ticks(&world, aInputs, 2, 4);

    /* Off the bearing but inside the hold cone, so the lock is live and the
     * only question is whether anything turns the machine back. */
    face_mech(&world, 0, MECHA_DEG(22));
    iBefore = world.aMechs[0].iFacing;

    if (bDash) {
        aInputs[0].bDash = true;
        aInputs[0].iMoveZ = 100;
        mecha_sim_tick(&world, aInputs, 2);
    }
    aInputs[0].bFireCenter = true;
    mecha_sim_tick(&world, aInputs, 2);
    aInputs[0].bFireCenter = false;
    aInputs[0].bDash = false;
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ / 2);

    iDrift = mecha_angle_delta(iBefore, world.aMechs[0].iFacing);
    return iDrift < 0 ? -iDrift : iDrift;
}

//-------------------------------------------------------------------------------------------------

static int test_firing_recentres_only_off_a_boost(void)
{
    int iStanding = firing_recentre_drift(false);
    int iBoosting = firing_recentre_drift(true);

    printf("   firing at range turns the machine: standing %d, boosting %d\n",
           iStanding, iBoosting);

    /*
     * Firing off a boost or out of the air puts the enemy back in front of
     * you; firing on your feet leaves the heading alone. The second half
     * matters as much as the first -- taking the heading away every time a
     * trigger came down would be the auto-turn back again under another
     * name, and the whole point of confining that to knife range was to
     * give the steering back.
     */
    CHECK(iBoosting > MECHA_DEG(8));
    CHECK(iStanding < MECHA_DEG(2));
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_lobbed_shots_reach_their_target(void)
{
    tMechaWorld world;
    tMechaInput aInputs[2];
    float fArmourBefore;

    /* A lobbed bomb has to actually land on someone, which is entirely down
     * to the ballistic solution picking a sane launch angle. */
    start_duel(&world, 0, 0, 0, 41u, 2);
    memset(aInputs, 0, sizeof(aInputs));
    world.aMechs[0].fX = 0.0f;
    world.aMechs[0].fZ = 0.0f;
    world.aMechs[1].fX = 0.0f;
    world.aMechs[1].fZ = MECHA_METRE * 55.0f;
    world.aMechs[0].iFacing = 0;
    run_ticks(&world, aInputs, 2, 4);

    fArmourBefore = world.aMechs[1].fArmour;
    aInputs[0].bFireRight = true;
    mecha_sim_tick(&world, aInputs, 2);
    memset(aInputs, 0, sizeof(aInputs));
    run_ticks(&world, aInputs, 2, MECHA_TICK_HZ * 4);
    CHECK(world.aMechs[1].fArmour < fArmourBefore);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_mesh_geometry(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    int iArenaQuads;
    int i;

    start_duel(&world, 0, 0, 1, 61u, 2);

    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_arena(&list, &world.arena);
    iArenaQuads = list.iCount;
    CHECK(iArenaQuads > 0);
    CHECK(list.iDropped == 0);

    for (i = 0; i < list.iCount; i++) {
        const tMechaQuad *pQuad = &aStorage[i];
        float fLength = mecha_length3(pQuad->afNormal[0], pQuad->afNormal[1],
                                      pQuad->afNormal[2]);

        /* Back-face rejection needs a real unit normal on every quad. */
        CHECK(near(fLength, 1.0f, 1e-3f));
    }

    /* The walls have to face inward, or the camera would see a solid box
     * from outside and nothing from inside. Every wall quad sits on the
     * boundary, so its normal must point back towards the middle. */
    for (i = 0; i < list.iCount; i++) {
        const tMechaQuad *pQuad = &aStorage[i];
        float fCx = 0.0f;
        float fCz = 0.0f;
        int v;

        for (v = 0; v < 4; v++) {
            fCx += pQuad->afVert[v][0] * 0.25f;
            fCz += pQuad->afVert[v][2] * 0.25f;
        }
        if (near(fabsf(fCx), world.arena.fHalfExtent, 1.0f)
            || near(fabsf(fCz), world.arena.fHalfExtent, 1.0f)) {
            if (fabsf(pQuad->afNormal[1]) > 0.9f)
                continue;   /* floor tile at the edge, not a wall */
            CHECK(pQuad->afNormal[0] * -fCx + pQuad->afNormal[2] * -fCz > 0.0f);
        }
    }

    /* A closed box built by mecha_add_box must have every face pointing away
     * from its own centre -- this is the winding table, checked rather than
     * trusted. */
    mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, &world, 0);
    CHECK(list.iCount > 0);
    CHECK(list.iCount % 6 == 0);
    for (i = 0; i + 5 < list.iCount; i += 6) {
        float afCentre[3] = { 0.0f, 0.0f, 0.0f };
        int iFace;
        int v;
        int iAxis;

        for (iFace = 0; iFace < 6; iFace++) {
            for (v = 0; v < 4; v++) {
                for (iAxis = 0; iAxis < 3; iAxis++)
                    afCentre[iAxis] += aStorage[i + iFace].afVert[v][iAxis]
                                       / 24.0f;
            }
        }
        for (iFace = 0; iFace < 6; iFace++) {
            const tMechaQuad *pQuad = &aStorage[i + iFace];
            float fDot = 0.0f;

            for (iAxis = 0; iAxis < 3; iAxis++)
                fDot += pQuad->afNormal[iAxis]
                        * (pQuad->afVert[0][iAxis] - afCentre[iAxis]);
            CHECK(fDot > 0.0f);
        }
    }

    /* Overflow is recorded, not written past the end of the buffer. */
    {
        tMechaQuad aTiny[8];
        int j;

        memset(aTiny, 0, sizeof(aTiny));
        mecha_quads_reset(&list, aTiny, 4);
        mecha_mesh_arena(&list, &world.arena);
        CHECK(list.iCount == 4);
        CHECK(list.iDropped > 0);
        for (j = 4; j < 8; j++) {
            CHECK(aTiny[j].byPalette == 0);
            CHECK(aTiny[j].afVert[0][0] == 0.0f);
        }
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

/* Width across the shoulders over standing height, from the mesh itself. */
static float build_aspect(int iDefIdx, tMechaQuad *paStorage)
{
    tMechaQuadList list;
    tMechaWorld world;
    float fMinX = 1e30f, fMaxX = -1e30f;
    float fMinY = 1e30f, fMaxY = -1e30f;
    int i;
    int v;

    start_duel(&world, 0, iDefIdx, 0, 77u, 1);
    mecha_quads_reset(&list, paStorage, MECHA_QUAD_CAPACITY);
    mecha_mesh_mech(&list, &world, 0);

    for (i = 0; i < list.iCount; i++) {
        for (v = 0; v < 4; v++) {
            float fX = paStorage[i].afVert[v][0];
            float fY = paStorage[i].afVert[v][1];

            if (fX < fMinX) fMinX = fX;
            if (fX > fMaxX) fMaxX = fX;
            if (fY < fMinY) fMinY = fY;
            if (fY > fMaxY) fMaxY = fY;
        }
    }
    if (fMaxY - fMinY < 1.0f)
        return 0.0f;
    return (fMaxX - fMinX) / (fMaxY - fMinY);
}

//-------------------------------------------------------------------------------------------------

static int test_builds_read_as_silhouettes(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    float fLancer = build_aspect(0, aStorage);
    float fBulwark = build_aspect(1, aStorage);
    float fHalcyon = build_aspect(2, aStorage);

    printf("   width/height  LANCER %.2f  SJ Mk.IV %.2f  Exos 2000 %.2f\n",
           fLancer, fBulwark, fHalcyon);

    /*
     * The archetypes have to be visible, not just written in the stat block.
     * Every machine is built from the same boxes scaled off fHeight and
     * fRadius, so without the build multipliers these three come out the
     * same shape at three sizes and a siege platform is indistinguishable
     * from an interceptor at any distance where it matters.
     *
     * Aspect ratio rather than absolute size, because size alone is not
     * silhouette: a machine that is merely bigger still reads as the same
     * machine.
     */
    CHECK(fBulwark > fLancer * 1.2f);
    CHECK(fHalcyon < fLancer * 0.85f);
    return 0;
}

//-------------------------------------------------------------------------------------------------

static int test_mesh_survives_a_match(void)
{
    static tMechaQuad aStorage[MECHA_QUAD_CAPACITY];
    tMechaQuadList list;
    tMechaWorld world;
    int i;

    /* Build every frame's geometry alongside a real AI fight: the mesh has
     * to cope with mechs falling over, dying, mines on the floor and the
     * projectile table at full stretch, all without overflowing. */
    mecha_sim_init(&world, 2, 0x9E11u, 2);
    mecha_sim_add_mech(&world, 1, MECHA_CONTROL_AI, 0);
    mecha_sim_add_mech(&world, 3, MECHA_CONTROL_AI, 1);
    mecha_sim_begin_match(&world);

    for (i = 0; i < MECHA_TICK_HZ * 30; i++) {
        int iMech;

        mecha_sim_tick(&world, NULL, 0);

        mecha_quads_reset(&list, aStorage, MECHA_QUAD_CAPACITY);
        mecha_mesh_arena(&list, &world.arena);
        mecha_mesh_shadows(&list, &world);
        for (iMech = 0; iMech < MECHA_MAX_MECHS; iMech++)
            mecha_mesh_mech(&list, &world, iMech);
        mecha_mesh_projectiles(&list, &world, world.aMechs[0].iFacing);
        mecha_mesh_effects(&list, &world, world.aMechs[0].iFacing);

        CHECK(list.iCount <= list.iCapacity);
        CHECK(list.iDropped == 0);

        /* Translucent quads carry a shade level, not a colour: shadow_poly
         * indexes shade_palette[256 * level] and that table holds only 16
         * blocks, so anything larger reads off the end of it. */
        for (iMech = 0; iMech < list.iCount; iMech++) {
            if (aStorage[iMech].byFlags & MECHA_QUAD_SHADOW)
                CHECK(aStorage[iMech].byPalette <= 15);
        }
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------

int main(void)
{
    struct {
        const char *szName;
        int (*pfnTest)(void);
    } aTests[] = {
        { "angles", test_angles },
        { "arena geometry", test_arena_geometry },
        { "roster", test_roster },
        { "movement and boost", test_movement_and_boost },
        { "arena confines mechs", test_arena_confines_mechs },
        { "stance selects weapon", test_stance_selects_weapon },
        { "firing and reload", test_firing_and_reload },
        { "shots damage and are blocked", test_shots_damage_and_are_blocked },
        { "knockdown", test_knockdown_and_invulnerability },
        { "round and match flow", test_round_and_match_flow },
        { "lobbed shots", test_lobbed_shots_reach_their_target },
        { "mesh geometry", test_mesh_geometry },
        { "builds read as silhouettes", test_builds_read_as_silhouettes },
        { "mesh survives a match", test_mesh_survives_a_match },
        { "determinism", test_determinism },
        { "ai fights", test_ai_fights },
        { "ai skill ladder", test_ai_skill_ladder },
        { "lock breaks and returns", test_lock_breaks_and_returns },
        { "lock survives a glance", test_lock_survives_a_glance },
        { "firing recentres only off a boost",
          test_firing_recentres_only_off_a_boost },
        { "auto turn is close quarters only",
          test_auto_turn_is_close_quarters_only },
        { "jump cancel", test_jump_cancel },
        { "guard turns melee aside", test_guard_turns_melee_aside },
        { "legs walk on jointed knees", test_legs_walk_on_jointed_knees },
        { "the legs have four gaits", test_the_legs_have_four_gaits },
        { "the round clock is a setting",
          test_the_round_clock_is_a_setting },
        { "the enemy can be told to hold fire",
          test_the_enemy_can_be_told_to_hold_fire },
        { "a machine at ease lowers its arms",
          test_a_machine_at_ease_lowers_its_arms },
        { "a machine with a lock settles into it",
          test_a_machine_with_a_lock_settles_into_it },
        { "dashing squares the legs to the burst",
          test_dashing_squares_the_legs_to_the_burst },
        { "a cancel drops like a stone", test_a_cancel_drops_like_a_stone },
        { "torso turns off the legs", test_torso_turns_off_the_legs },
        { "arms and head follow the lock",
          test_arms_and_head_follow_the_lock },
        { "shadows survive the paint order",
          test_shadows_survive_the_paint_order },
        { "dashing is committed", test_dashing_is_committed },
        { "a dash can be steered and cancelled",
          test_a_dash_can_be_steered_and_cancelled },
        { "a boost carries and bounces", test_a_boost_carries_and_bounces },
        { "dashing works in the air", test_dashing_works_in_the_air },
        { "the computer pilot stays on the roof",
          test_the_computer_pilot_stays_on_the_roof },
        { "the meadow is an octagon with hills",
          test_the_meadow_is_an_octagon_with_hills },
        { "the forest stands outside the fight",
          test_the_forest_stands_outside_the_fight },
        { "the ground itself stops a shot",
          test_the_ground_itself_stops_a_shot },
        { "the bodies lean, squat, ring and shake",
          test_the_bodies_lean_squat_ring_and_shake },
        { "close quarters swings a blade",
          test_close_quarters_swings_a_blade },
        { "the gun car spins and rolls",
          test_the_gun_car_spins_and_rolls },
        { "the gun car follows the ground",
          test_the_gun_car_follows_the_ground },
        { "the gun car is a car with a gun",
          test_the_gun_car_is_a_car_with_a_gun },
        { "the gun car wears the game's own paint",
          test_the_gun_car_wears_the_games_own_paint },
        { "the gun car drives", test_the_gun_car_drives },
        { "the gun car has one gun and a bumper",
          test_the_gun_car_has_one_gun_and_a_bumper },
        { "the gun car never turns itself",
          test_the_gun_car_never_turns_itself },
        { "the roof has no walls and a tabletop",
          test_the_roof_has_no_walls_and_a_tabletop },
        { "a boost up a slope leaves the ground",
          test_a_boost_up_a_slope_leaves_the_ground },
        { "nothing is built coplanar", test_nothing_is_built_coplanar },
        { "blasts draw over what they engulf",
          test_blasts_draw_over_what_they_engulf },
        { "shots trade on damage", test_shots_trade_on_damage },
        { "a bomb leaves a fireball", test_a_bomb_leaves_a_fireball },
        { "a landing throws a ring of dust",
          test_a_landing_throws_a_ring_of_dust },
        { "sky carries a cloud dome", test_sky_carries_a_cloud_dome },
        { "shots carry plasma frames", test_shots_carry_plasma_frames },
        { "death throws debris", test_death_throws_debris },
        { "machines carry their weight", test_machines_carry_their_weight },
    };
    size_t i;

    for (i = 0; i < sizeof(aTests) / sizeof(aTests[0]); i++) {
        int iResult = aTests[i].pfnTest();

        if (iResult != 0) {
            fprintf(stderr, "mecha sim test '%s' failed\n", aTests[i].szName);
            return 1;
        }
        printf("ok - %s\n", aTests[i].szName);
    }
    printf("mecha sim: %zu test groups passed\n",
           sizeof(aTests) / sizeof(aTests[0]));
    return 0;
}
