"""Tests of sprite_colours.py on GIFs written here (no personal file enters
the repository): python3 -m unittest discover tools."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sprite_colours  # noqa: E402
from test_sprite_pack import gif  # noqa: E402

BLACK, RED, GREEN, BLUE = (0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)
PAL4 = [BLACK, RED, GREEN, BLUE]
GREY, DARK_RED = (128, 128, 128), (200, 20, 20)
PAL8 = [BLACK, RED, GREEN, BLUE, GREY, DARK_RED, (255, 255, 255), (0, 0, 0)]


def frame(pixels, w, h, **kw):
    return dict(rect=(0, 0, w, h), pixels=bytes(pixels), **kw)


class WeightsTest(unittest.TestCase):
    def test_pixels_times_delay_transparent_for_nothing(self):
        # Frame 1 (delay 0 plays as 10): 2 red, 1 green, 1 transparent.
        # Frame 2 (delay 30), a transparent hole over the second pixel: 1 red, 1 green... the
        # composited canvas keeps frame 1's red there, so 2 red, 1 green, 1 transparent.
        data = gif(4, 1, PAL4, [
            frame([1, 1, 2, 0], 4, 1, transparent=0),
            frame([1, 0, 2, 0], 4, 1, transparent=0, delay=30),
        ])
        import gifpics
        w = sprite_colours.weights(gifpics.animation(data))
        self.assertEqual(w, {RED: 2 * 10 + 2 * 30, GREEN: 1 * 10 + 1 * 30})

    def test_merge_joins_a_near_colour_to_the_heavier_one(self):
        merged = sprite_colours.merge({RED: 5, DARK_RED: 4, BLUE: 3})
        self.assertEqual(merged, [(RED, 9), (BLUE, 3)])
        self.assertEqual(sprite_colours.merge({RED: 5, DARK_RED: 4, BLUE: 3}, 10),
                         [(RED, 5), (DARK_RED, 4), (BLUE, 3)], "a smaller distance keeps them apart")
        self.assertEqual(sprite_colours.merge({RED: 3, DARK_RED: 4}), [(DARK_RED, 7)], "the heavier survives")

    def test_lively(self):
        self.assertTrue(sprite_colours.lively(RED))
        self.assertFalse(sprite_colours.lively(BLACK))
        self.assertFalse(sprite_colours.lively(GREY), "no saturation")
        self.assertFalse(sprite_colours.lively((60, 10, 10)), "too dark")
        self.assertTrue(sprite_colours.lively((64, 48, 48)), "a quarter each, just")


class ColoursTest(unittest.TestCase):
    def test_heaviest_first_and_count(self):
        data = gif(6, 1, PAL4, [frame([1, 1, 1, 2, 2, 3], 6, 1)])
        self.assertEqual(sprite_colours.colours(data), [RED, GREEN, BLUE])
        self.assertEqual(sprite_colours.colours(data, 2), [RED, GREEN])

    def test_a_lively_colour_leads_black_and_grey(self):
        # Black 4, grey 3, red 2, blue 1: red leads, the rest keep their order.
        data = gif(10, 1, PAL8, [frame([0, 0, 0, 0, 4, 4, 4, 1, 1, 3], 10, 1)])
        self.assertEqual(sprite_colours.colours(data), [RED, BLACK, GREY, BLUE])

    def test_greys_alone_keep_their_order(self):
        data = gif(3, 1, PAL8, [frame([4, 4, 0], 3, 1)])
        self.assertEqual(sprite_colours.colours(data), [GREY, BLACK])

    def test_a_lively_colour_past_count_does_not_lead(self):
        data = gif(4, 1, PAL8, [frame([0, 0, 4, 1], 4, 1)])
        self.assertEqual(sprite_colours.colours(data, 2), [BLACK, GREY])

    def test_cli(self):
        data = gif(3, 1, PAL4, [frame([3, 3, 1], 3, 1)])
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "a.gif")
            with open(path, "wb") as f:
                f.write(data)
            out = []
            real = sys.stdout
            try:
                import io
                sys.stdout = io.StringIO()
                self.assertEqual(sprite_colours.main([path]), 0)
                self.assertEqual(sprite_colours.main(["--count", "1", path]), 0)
                out = sys.stdout.getvalue().splitlines()
            finally:
                sys.stdout = real
            self.assertEqual(out, ["0000FF FF0000", "0000FF"])
            with open(path, "wb") as f:
                f.write(b"not a gif")
            with open(os.devnull, "w") as null:
                real_err, sys.stderr = sys.stderr, null
                try:
                    self.assertEqual(sprite_colours.main([path]), 2)
                    self.assertEqual(sprite_colours.main(["--count", "0", path]), 2)
                    self.assertEqual(sprite_colours.main([]), 2)
                    self.assertEqual(sprite_colours.main([os.path.join(d, "none.gif")]), 2)
                finally:
                    sys.stderr = real_err


if __name__ == "__main__":
    unittest.main()
