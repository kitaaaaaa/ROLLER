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
        /* Crouching is the fast refill; that is the whole reason to do it. */
        CHECK(pDef->iBoostCrouchRegen > pDef->iBoostRegen);

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
    int iBoostAfterCrouch;

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

    /* Standing refills, crouching refills faster. Both start from the same
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
    aInputs[0].bCrouch = true;
    run_ticks(&world, aInputs, 2, 30);
    iBoostAfterCrouch = world.aMechs[0].iBoost;
    CHECK(world.aMechs[0].byMove == MECHA_MOVE_CROUCH);
    CHECK(iBoostAfterCrouch < pDef->iBoostMax * MECHA_TICK_HZ);
    CHECK(iBoostAfterCrouch > iBoostAfterStand);

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
    const tMechaWeaponDef *pCrouched;

    start_duel(&world, 0, 0, 1, 9u, 2);
    memset(aInputs, 0, sizeof(aInputs));

    CHECK(mecha_mech_stance(&world.aMechs[0]) == MECHA_STANCE_STAND);
    pStanding = mecha_mech_weapon(&world, 0, MECHA_SLOT_CENTER);
    CHECK(pStanding != NULL);

    aInputs[0].bCrouch = true;
    mecha_sim_tick(&world, aInputs, 2);
    CHECK(mecha_mech_stance(&world.aMechs[0]) == MECHA_STANCE_CROUCH);
    pCrouched = mecha_mech_weapon(&world, 0, MECHA_SLOT_CENTER);
    CHECK(pCrouched != NULL);

    /* The same trigger has to be a different attack in a different stance --
     * that is the mode's central rule. */
    CHECK(pCrouched != pStanding);
    CHECK(strcmp(pCrouched->szName, pStanding->szName) != 0);
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
        aInputs[0].bCrouch = (i % 210) < 30;
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
            if (world.match.byPhase == MECHA_PHASE_MATCH_OVER)
                break;
        }
        CHECK(bDamaged);
        CHECK(world.aMechs[0].fDamageDealt + world.aMechs[1].fDamageDealt > 0.0f);
    }
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
        { "mesh survives a match", test_mesh_survives_a_match },
        { "determinism", test_determinism },
        { "ai fights", test_ai_fights },
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
