"""Leaving the arena has to reach the menus.

"Exit to Whiplash" aborted the process. The cause was not in the exit path at
all: on the way *in*, the arena called `setpal("palette.pal")` and then pointed
`pal_addr` at the static `palette[]` array, on the strength of a note in the
GPU renderer saying setpal leaves that pointer alone. It does not -- this
reimplementation of setpal frees whatever `pal_addr` holds, loads the file, and
points `pal_addr` and `pal_selector` at the buffer it just read. Overwriting it
afterwards leaked that buffer and left `pal_addr` holding the address of a
static array, so the next setpal anybody called -- the main menu's, on the way
out -- passed a static to free() and libc aborted the process.

The crash needs the retail data and a whole game boot to reproduce, which is
what the `arena-exit` snapshot scene is for. What this file pins is the shape
of the bug, so it cannot come back quietly: that the mode does not assign
`pal_addr` after a setpal, that setpal really does own that pointer, and that
the scene which walks the path is still wired up.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"
MODE = ROLLER / "mecha_mode.c"
SOUND = ROLLER / "sound.c"
SCENES = ROLLER / "snapshot_scenes.c"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class SetpalOwnsThePalettePointer(unittest.TestCase):
    def test_setpal_frees_and_reassigns_pal_addr(self) -> None:
        """The premise the rest of this file rests on."""
        source = read(SOUND)
        body = source[source.index("bool setpal("):]
        body = body[: body.index("\n}\n")]
        self.assertIn("free(pal_addr)", body.replace(" ", ""))
        self.assertIn("pal_addr = pFileData", body)

    def test_the_arena_never_leaves_a_static_for_setpal_to_free(self) -> None:
        """The bug itself: a static array's address handed to free().

        Putting the saved pointer back is the mode returning what it
        borrowed. Anything else is the mode claiming ownership it has not
        got -- allowed only when it also marks the selector negative, which
        is the engine's own way of saying this memory is nobody's to free.
        """
        source = read(MODE)
        for match in re.finditer(r"^\s*pal_addr\s*=\s*(\S+);", source,
                                 re.MULTILINE):
            target = match.group(1)
            if target == "s_pSavedPalAddr":
                continue
            following = source[match.end():match.end() + 200]
            self.assertIn(
                "pal_selector = (void *)-1;", following,
                "mecha_mode.c assigns pal_addr = %s without marking the "
                "selector; setpal will try to free() it" % target)

    def test_the_saved_pointer_is_taken_where_it_is_used(self) -> None:
        """A setpal that freed and then failed leaves the old value dangling,
        so the value put back has to be read after any setpal attempt, not
        before it."""
        source = read(MODE)
        install = source.index("pal_addr = s_aArenaPalette;")
        setpal = source.index('setpal("palette.pal")')
        capture = source.rindex("s_pSavedPalAddr = pal_addr;", 0, install)
        self.assertGreater(capture, setpal)


class TheExitPathIsWalkable(unittest.TestCase):
    def test_the_scene_is_registered(self) -> None:
        self.assertIn('"arena-exit"', read(SCENES))
        self.assertIn("snapshot_render_arena_exit", read(SCENES))

    def test_the_scene_goes_all_the_way_to_the_menu(self) -> None:
        """Entering and leaving proves nothing on its own -- the crash was in
        what the menus did afterwards."""
        source = read(MODE)
        body = source[source.index("void snapshot_render_arena_exit("):]
        body = body[: body.index("\n}\n")]
        for call in ("mecha_mode_enter", "mecha_mode_exit",
                     "frontend_menu_enter", "frontend_menu_update"):
            self.assertIn(call, body)


if __name__ == "__main__":
    unittest.main()
