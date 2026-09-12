"""Team deathmatch: sixteen machines, two sides, one colour each.

The fight itself is the simulation's and is measured by the C tests. What
cannot be measured there is the wiring in front of it: that the briefing
offers the mode, that it seats eight a side in the order the arena hands out
its spawn points, that a side shares one paint scheme without the two sides
sharing it with each other, and that the result line asks which side won
rather than which machine. See docs/arena-notes.md [MODE-08].
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODE = ROOT / "PROJECTS" / "ROLLER" / "mecha_mode.c"


def read() -> str:
    return MODE.read_text(encoding="utf-8")


def body(source: str, signature: str) -> str:
    """One function, from its signature to the closing brace in column one."""
    start = source.index(signature)
    return source[start : source.index("\n}\n", start)]


class TheBriefingOffersTheMode(unittest.TestCase):
    def test_the_mode_is_one_of_the_game_modes(self) -> None:
        source = read()
        modes = source[source.index("MECHA_GAME_DUEL = 0") :]
        modes = modes[: modes.index("MECHA_GAME_COUNT")]
        self.assertIn("MECHA_GAME_TEAM", modes)

    def test_the_mode_has_a_name_of_its_own(self) -> None:
        """The row draws the name, so an unnamed mode reads as a duel."""
        names = body(read(), "static const char *mecha_mode_game_name")
        self.assertRegex(names, r'case MECHA_GAME_TEAM:\s*return "[^"]+";')


class TheSidesAreSeatedAlternately(unittest.TestCase):
    def test_the_arena_is_filled(self) -> None:
        start = body(read(), "static void mecha_mode_start_match")
        team = start[start.index("MECHA_GAME_TEAM") :]
        self.assertIn("iSlot < MECHA_MAX_MECHS", team)

    def test_the_seats_alternate_sides(self) -> None:
        """A round hands out spawn points in seat order and both arena spawn
        shapes split a duel by that order, so seating one side first puts
        half of it in the enemy's base."""
        start = body(read(), "static void mecha_mode_start_match")
        team = start[start.index("MECHA_GAME_TEAM") :]
        add = team[team.index("mecha_sim_add_mech") :]
        add = add[: add.index(";")]
        self.assertIn("(uint8)(iSlot & 1)", add)
        self.assertIn("(iSlot & 1) ? s_iOpponentDef : s_iPlayerDef", add)

    def test_the_free_for_all_is_untouched(self) -> None:
        """Survival still puts everyone on a team of their own."""
        start = body(read(), "static void mecha_mode_start_match")
        survival = start[start.index("MECHA_GAME_SURVIVAL") :]
        survival = survival[: survival.index("MECHA_GAME_TEAM")]
        self.assertIn("(uint8)(iSlot + 1)", survival)


class ASideFightsInOneColour(unittest.TestCase):
    def test_the_paint_is_only_laid_on_for_this_mode(self) -> None:
        start = body(read(), "static void mecha_mode_start_match")
        self.assertRegex(
            start,
            r"if \(s_iGameMode == MECHA_GAME_TEAM\)\s*\n"
            r"\s*mecha_mode_paint_teams\(\);",
        )

    def test_a_side_shares_one_scheme(self) -> None:
        """Two draws, one a side -- not one draw per machine."""
        paint = body(read(), "static void mecha_mode_paint_teams")
        self.assertEqual(len(re.findall(r"mecha_rng_range\(", paint)), 1)
        self.assertIn("pMech->byTeam == 0 ? (uint8_t)s_iScheme", paint)

    def test_the_player_s_side_wears_what_the_briefing_chose(self) -> None:
        paint = body(read(), "static void mecha_mode_paint_teams")
        self.assertIn("s_iScheme", paint)

    def test_the_two_sides_are_never_the_same_colour(self) -> None:
        """One colour on both sides is the one thing the mode cannot have."""
        paint = body(read(), "static void mecha_mode_paint_teams")
        self.assertIn("iFoe == s_iScheme % iSchemes", paint)
        self.assertIn("iFoe = (iFoe + 1) % iSchemes", paint)

    def test_the_draw_does_not_disturb_the_fight(self) -> None:
        """The world's own stream decides how the fight goes; paint drawn
        from it would shift every shot after it. [SIM-22]"""
        paint = body(read(), "static void mecha_mode_paint_teams")
        self.assertIn("tMechaRng paint;", paint)
        self.assertIn("mecha_rng_seed(&paint,", paint)
        self.assertNotIn("&s_World.rng", paint)


class TheResultIsTheSide_s(unittest.TestCase):
    def test_a_win_is_a_win_for_the_player_s_side(self) -> None:
        source = read()
        self.assertNotIn("iWinner == s_iPlayerIdx", source)
        self.assertIn("mecha_mech_allied(&s_World, s_iPlayerIdx, iWinner)",
                      source)


if __name__ == "__main__":
    unittest.main()
