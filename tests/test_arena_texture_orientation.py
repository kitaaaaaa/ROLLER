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

The second is in the mesh, and it is about the body rather than the paint.
The race game's frame is right-handed -- x along the car, y across it, z up
-- and the arena's is not: x across, y up, z forward. Swapping the three
axes without negating one builds the car's mirror image, with its wheel
arches, its exhausts and both flanks of its livery on the wrong sides. The
lateral axis is negated to put that right.

The two interact, which is what made them hard to separate. Reflecting the
body reverses every winding the plan had, so retail geometry reaches POLYTEX
already turned round once -- and turning it round again, the way the mode's
own quads need, is what put ZIZIN on the car as NIZIZ. So a quad carrying
artwork out of the game's data says so, and keeps the order it arrived in.

Neither is measurable from a headless C test -- both live past the
renderer's boundary and are only visible as pixels -- so what this pins is
that both are still there and still opposite to each other, with the
reasoning above being the part worth keeping.
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


class TexturedQuadsAreTurnedRoundOnTheWayOut(unittest.TestCase):
    def test_the_mode_s_own_quads_are_reversed(self) -> None:
        """The one line that puts every tile in the mode the right way up."""
        source = read(RENDER)
        block = body(source, "static void mecha_render_scene")
        self.assertIn("3 - iCorner", block)

    def test_retail_artwork_keeps_the_order_it_arrived_in(self) -> None:
        """Its geometry was reflected on the way in; that is the flip."""
        source = read(RENDER)
        block = body(source, "static void mecha_render_scene")
        self.assertIn("MECHA_QUAD_TEX_FLIP", block)
        self.assertRegex(block, r"MECHA_QUAD_TEX_FLIP\)\s*\?\s*iCorner")

    def test_it_only_touches_the_textured_path(self) -> None:
        """Flat fills must keep the winding their normals were built from."""
        source = read(RENDER)
        block = body(source, "static void mecha_render_scene")
        turned_at = block.index("3 - iCorner")
        flat_at = block.index("game_render_quad_world(pRenderer, aVerts,"
                              " TEXTURE_HANDLE_INVALID")
        self.assertLess(turned_at, flat_at)
        # The flat call hands over the untouched array.
        self.assertNotIn("aTexVerts", block[flat_at:])


class TheCarPlanIsReflectedIntoTheArena(unittest.TestCase):
    def test_the_lateral_axis_is_negated(self) -> None:
        """Without it the car is built as its own mirror image."""
        source = read(MESH)
        block = body(source, "static void mecha_add_zizin_body")
        self.assertRegex(block, r"mecha_pose_apply\(pPose,\s*-pPlan->fY")

    def test_exactly_one_axis_is_negated(self) -> None:
        """A second negation is a rotation, and puts the mirror back."""
        source = read(MESH)
        block = body(source, "static void mecha_add_zizin_body")
        call = re.search(r"mecha_pose_apply\(pPose,(.*?)\);", block,
                         re.S).group(1)
        self.assertEqual(call.count("-pPlan->"), 1)
        for axis in ("pPlan->fX", "pPlan->fY", "pPlan->fZ"):
            self.assertIn(axis, call)

    def test_the_body_asks_for_the_mirrored_tiles(self) -> None:
        source = read(MESH)
        block = body(source, "static void mecha_add_zizin_body")
        self.assertIn("MECHA_QUAD_TEX_FLIP", block)


if __name__ == "__main__":
    unittest.main()
