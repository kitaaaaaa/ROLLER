"""The briefing's rows have to be wired all the way through.

Three settings were added to a screen that already had four, and each of
them is only useful if it reaches the thing it names: the round clock has to
arrive at the simulation, the debug switch has to arrive at the pilots, and
the controls row has to lead somewhere. A row that draws but does nothing is
worse than no row, because it looks like it worked.

None of that needs a window, and none of it is arithmetic the C tests can
measure -- it is wiring. So what this pins is the wiring: that every row the
briefing draws is a row the update loop answers to, that the two new
settings are passed to the simulation when a match starts, and that the
controls screen can be both entered and left.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"
MODE = ROLLER / "mecha_mode.c"
RENDER = ROLLER / "mecha_render.c"
RENDER_H = ROLLER / "mecha_render.h"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def rows() -> list[str]:
    """The row ids, in the order the enum declares them."""
    source = read(MODE)
    body = source[source.index("MECHA_ROW_START = 0"):]
    body = body[: body.index("MECHA_ROW_COUNT")]
    return re.findall(r"MECHA_ROW_[A-Z_]+", body)


class EveryRowIsDrawnAndAnswered(unittest.TestCase):
    def test_every_row_gets_a_label(self) -> None:
        source = read(MODE)
        build = source[source.index("mecha_mode_build_briefing"):]
        build = build[: build.index("\n}\n")]
        for row in rows():
            self.assertIn(f"pBrief->aRows[{row}].szLabel", build,
                          f"{row} is declared but never labelled")

    def test_the_briefing_can_hold_every_row(self) -> None:
        """MECHA_BRIEF_MAX_ROWS clips silently, which loses the exit."""
        header = read(RENDER_H)
        cap = int(re.search(r"MECHA_BRIEF_MAX_ROWS\s+(\d+)", header).group(1))
        self.assertGreaterEqual(cap, len(rows()))

    def test_the_settings_rows_are_answered(self) -> None:
        """A row with a value has to do something when it is pushed."""
        source = read(MODE)
        update = source[source.index("mecha_mode_update_briefing"):]
        update = update[: update.index("\n}\n")]
        for row in ("MECHA_ROW_MECH", "MECHA_ROW_OPPONENT", "MECHA_ROW_ARENA",
                    "MECHA_ROW_SKILL", "MECHA_ROW_TIME",
                    "MECHA_ROW_HOLD_FIRE"):
            self.assertIn(f"case {row}:", update,
                          f"{row} draws a value nothing changes")


class TheNewSettingsReachTheSimulation(unittest.TestCase):
    def test_the_match_carries_the_clock_and_the_hold(self) -> None:
        source = read(MODE)
        start = source[source.index("static void mecha_mode_start_match"):]
        start = start[: start.index("\n}\n")]
        self.assertIn("mecha_sim_set_round_seconds", start)
        self.assertIn("mecha_sim_set_ai_hold_fire", start)

    def test_deathmatch_is_the_zero_the_simulation_reads(self) -> None:
        """The choices the row cycles, one of which switches the clock off."""
        source = read(MODE)
        choices = re.search(r"s_aiRoundSeconds\[\]\s*=\s*\{([^}]*)\}", source)
        values = [int(v) for v in choices.group(1).replace(" ", "").split(",")
                  if v]
        self.assertEqual(values, [30, 60, 90, 120, 0])


class TheControlsPageIsReachable(unittest.TestCase):
    def test_the_row_opens_it_and_something_closes_it(self) -> None:
        source = read(MODE)
        self.assertIn("s_eScreen = MECHA_SCREEN_CONTROLS", source)
        close = source[source.index("mecha_mode_update_controls"):]
        close = close[: close.index("\n}\n")]
        self.assertIn("s_eScreen = MECHA_SCREEN_BRIEFING", close)

    def test_the_page_is_drawn_and_the_briefing_no_longer_draws_it(self) -> None:
        """The whole point was getting ten lines off the first screen."""
        self.assertIn("mecha_render_controls(scrbuf", read(MODE))
        source = read(RENDER)
        brief = source[source.index("void mecha_render_briefing("):]
        self.assertNotIn("s_aaszControls", brief)


if __name__ == "__main__":
    unittest.main()
