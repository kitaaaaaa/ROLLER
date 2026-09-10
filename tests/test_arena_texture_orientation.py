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

The second is the car's alone, and it is a different flip. Its tiles come
out of the game's own data laid out for the way the race game hands its
polygons over, so turning them round like everything else in the mode leaves
them mirrored -- the flank reads NIZIZ. What they want is the corners
swapped in pairs, which is a plain horizontal mirror of the tile. Nothing
about the geometry needs touching to get there, and touching it would be
wrong: the axes only swap, so the body keeps the plan's own handedness and
its wheels, exhausts and livery stay on the sides they belong on.

Neither is measurable from a headless C test -- both live past the
renderer's boundary and are only visible as pixels -- so what this pins is
that both are still there and still different from each other, with the
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

    def test_retail_artwork_is_mirrored_instead(self) -> None:
        """Corners swapped in pairs, which is the horizontal flip."""
        source = read(RENDER)
        block = body(source, "static void mecha_render_scene")
        self.assertIn("MECHA_QUAD_TEX_FLIP", block)
        self.assertIn("(1 - iCorner) & 3", block)

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


class TheCarPlanIsNotReflected(unittest.TestCase):
    def test_the_axes_only_swap(self) -> None:
        """Negating one would mirror the body to fix the paint on it."""
        source = read(MESH)
        block = body(source, "static void mecha_add_zizin_body")
        call = re.search(r"mecha_pose_apply\(pPose,(.*?)\);", block,
                         re.S).group(1)
        self.assertEqual(call.count("-pPlan->"), 0)
        for axis in ("pPlan->fX", "pPlan->fY", "pPlan->fZ"):
            self.assertIn(axis, call)

    def test_the_body_asks_for_the_mirrored_tiles(self) -> None:
        source = read(MESH)
        block = body(source, "static void mecha_add_zizin_body")
        self.assertIn("MECHA_QUAD_TEX_FLIP", block)


if __name__ == "__main__":
    unittest.main()
