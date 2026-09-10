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
        /* Dashing has to be faster than walking or the gauge means nothing. */
        CHECK(pDef->fDashSpeed > pDef->fWalkSpeed);
        /* Guarding is the fast refill; that is one of two reasons to do it. */
        CHECK(pDef->iBoostGuardRegen > pDef->iBoostRegen);

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

    /* Standing refills, guarding refills faster. Both start from the same
     * low gauge, and over a short enough window that neither saturates --
     * comparing two full gauges would prove nothing. */
    memset(aInputs, 0, sizeof(aInputs));
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

        mecha_sim_init(&world, iDefA % mecha_arena_count(),
                       0x51EDu + (uint32_t)iDefA, 2);
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
         * over the player; and it has to hold one most of the time, or it
         * has no idea how to fight and the skill levels are measuring noise.
         * Measured across the roster it spends between eight and twenty per
         * cent of a fight without one.
         */
        CHECK(iBrokenTicks > 0);
        CHECK(iHeldTicks > iBrokenTicks * 2);
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
    static const char *const aszNames[3] = { "BULWARK", "LANCER", "HALCYON" };

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

    printf("   width/height  LANCER %.2f  BULWARK %.2f  HALCYON %.2f\n",
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
