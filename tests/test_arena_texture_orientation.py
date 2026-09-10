"""Retail artwork has to come out the right way round.

Two separate mistakes put the game's own art on screen mirrored, and neither
was visible until something with lettering on it turned up.

The first is in the renderer. POLYTEX derives its own texture coordinates
from the projected polygon, so the order the four corners arrive in decides
how the tile lies on them -- and the arena winds its quads the other way
round their faces from the way the race game winds the geometry in its data
files. Nothing else in the mode noticed, because this renderer rejects back
faces off the stored normal rather than off the projected winding: a quad
wound backwards still culls, sorts and fills correctly, and grass, tarmac,
concrete and a plasma bolt all look the same mirrored. Put a road sign on
one and it reads backwards.

The second is in the mesh. The race game's plans are in a right-handed frame
-- x along, y across, z up -- and the arena's is not: x across, y up, z
forward. Swapping the three axes without negating one maps the car onto its
own reflection, which is invisible on a car body right up until the
numberplate.

Neither is measurable from a headless C test: the first lives past the
renderer's boundary and the second is only visible as pixels. What is
checkable is that both fixes are still there, and that is what this pins --
the reasoning above being the part worth keeping.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"
RENDER = ROLLER / "mecha_render.c"
MESH = ROLLER / "mecha_mesh.c"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def body(source: str, marker: str) -> str:
    start = source.index(marker)
    return source[start : source.index("\n}\n", start)]


class TexturedQuadsAreHandedOverReversed(unittest.TestCase):
    def test_the_corner_order_is_turned_round(self) -> None:
        """The one line that puts every tile in the mode the right way up."""
        source = read(RENDER)
        block = body(source, "static void mecha_render_scene")
        self.assertIn("aTexVerts[iCorner] = aVerts[3 - iCorner];", block)

    def test_it_only_touches_the_textured_path(self) -> None:
        """Flat fills must keep the winding their normals were built from."""
        source = read(RENDER)
        block = body(source, "static void mecha_render_scene")
        reversed_at = block.index("aVerts[3 - iCorner]")
        flat_at = block.index("game_render_quad_world(pRenderer, aVerts,"
                              " TEXTURE_HANDLE_INVALID")
        self.assertLess(reversed_at, flat_at)
        # The flat call hands over the untouched array.
        self.assertNotIn("aTexVerts", block[flat_at:])


class TheCarPlanIsReflectedIntoTheArena(unittest.TestCase):
    def test_the_lateral_axis_is_negated(self) -> None:
        """Without this the whole car is its own mirror image."""
        source = read(MESH)
        block = body(source, "static void mecha_add_zizin_body")
        self.assertRegex(block, r"mecha_pose_apply\(pPose,\s*-pPlan->fY")

    def test_the_other_two_axes_only_swap(self) -> None:
        """A second negation would put it back, or stand it on its roof."""
        source = read(MESH)
        block = body(source, "static void mecha_add_zizin_body")
        call = re.search(r"mecha_pose_apply\(pPose,(.*?)\);", block,
                         re.S).group(1)
        self.assertEqual(call.count("-pPlan->"), 1)
        self.assertIn("pPlan->fZ", call)
        self.assertIn("pPlan->fX", call)


if __name__ == "__main__":
    unittest.main()
