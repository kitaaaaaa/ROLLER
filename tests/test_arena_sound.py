"""The arena's sound layer, checked where it can be checked without ears.

The mixer needs a device and the samples need FATDATA, so what is asserted
here is the wiring and the conventions: that the mode drives the sound layer
at the right moments, that the sim is still told nothing about sound, and
that the pan and volume constants are the ones Whiplash's own mixer expects.
See docs/arena-notes.md [SND-01].
"""

import re
import unittest
from pathlib import Path

ROLLER = Path(__file__).resolve().parents[1] / "PROJECTS" / "ROLLER"
SOUND = ROLLER / "mecha_sound.c"
SOUND_H = ROLLER / "mecha_sound.h"
MODE = ROLLER / "mecha_mode.c"
SIM = ROLLER / "mecha_sim.c"
RENDER_H = ROLLER / "mecha_render.h"


def read(path):
    return path.read_text(encoding="utf-8")


class ArenaSoundTests(unittest.TestCase):
    def test_mode_drives_the_sound_layer_at_the_right_moments(self):
        mode = read(MODE)
        self.assertIn('#include "mecha_sound.h"', mode)
        for call in (
            "mecha_sound_enter();",
            "mecha_sound_exit();",
            "mecha_sound_briefing();",
            "mecha_sound_match();",
            "mecha_sound_update(&s_World, &s_Camera);",
        ):
            self.assertIn(call, mode, call)

    def test_the_listener_is_placed_after_the_camera_moves(self):
        """A frame's sound has to be mixed from where that frame is drawn."""
        mode = read(MODE)
        camera = mode.index("mecha_mode_free_camera_update();")
        listener = mode.index("mecha_sound_update(&s_World, &s_Camera);")
        self.assertLess(camera, listener)

    def test_the_simulation_still_knows_nothing_about_sound(self):
        """The sim stays headless: the sound layer reads the world instead."""
        sim = read(SIM)
        self.assertNotIn("mecha_sound", sim)
        self.assertNotIn("sound.h", sim)

    def test_it_borrows_whiplash_samples_rather_than_shipping_its_own(self):
        sound = read(SOUND)
        for sample in (
            "SOUND_SAMPLE_ENGINE",
            "SOUND_SAMPLE_SKID1",
            "SOUND_SAMPLE_LANDSKID",
            "SOUND_SAMPLE_EXPLO",
            "SOUND_SAMPLE_BIGCRASH",
            "SOUND_SAMPLE_FENDER",
        ):
            self.assertIn(sample, sound, sample)

    def test_pan_runs_left_to_right(self):
        """DIGISetPanLocation reads iPan / 0x8000 - 1, so zero is hard left.

        The sign of the sine term is the whole convention: get it backwards
        and every machine is on the wrong side of the player.
        """
        sound = read(SOUND)
        self.assertRegex(
            sound, r"dPan\s*=\s*\(1\.0\s*\+\s*\(double\)mecha_sin\(",
        )
        self.assertIn("0x8000", sound)

    def test_loops_are_stopped_by_asking_for_no_volume(self):
        """loopsample() takes volume zero as stop; leaving them running would
        carry the arena's engines back into the race."""
        sound = read(SOUND)
        exit_body = sound[sound.index("void mecha_sound_exit(void)"):]
        exit_body = exit_body[: exit_body.index("\n}")]
        self.assertIn("loopsample(i, MECHA_SFX_ENGINE, 0, 0,", exit_body)
        self.assertIn("loopsample(i, MECHA_SFX_SKID, 0, 0,", exit_body)
        self.assertIn("stopmusic();", exit_body)

    def test_the_camera_basis_is_shared_rather_than_copied(self):
        """The sound layer needs the same axes the renderer uses; a second
        copy of that basis is a second thing to get wrong."""
        self.assertIn("void mecha_camera_basis(", read(RENDER_H))
        self.assertIn("mecha_camera_basis(pCamera,", read(SOUND))

    def test_every_entry_point_is_declared(self):
        header = read(SOUND_H)
        body = read(SOUND)
        for name in (
            "mecha_sound_enter",
            "mecha_sound_exit",
            "mecha_sound_briefing",
            "mecha_sound_match",
            "mecha_sound_update",
        ):
            self.assertIn(name, header, name)
            self.assertRegex(body, r"void\s+%s\s*\(" % re.escape(name))


if __name__ == "__main__":
    unittest.main()
