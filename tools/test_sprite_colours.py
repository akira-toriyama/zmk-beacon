"""Tests of sprite_colours.py on GIFs written here (no personal file enters
the repository): python3 -m unittest discover tools."""
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gifpics  # noqa: E402
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
        # Frame 2 (delay 30) paints a transparent hole over the second pixel,
        # which keeps frame 1's red: again 2 red, 1 green, 1 transparent.
        data = gif(4, 1, PAL4, [
            frame([1, 1, 2, 0], 4, 1, transparent=0),
            frame([1, 0, 2, 0], 4, 1, transparent=0, delay=30),
        ])
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
        self.assertFalse(sprite_colours.lively((64, 49, 49)), "a shade under a quarter of saturation")


class ColoursTest(unittest.TestCase):
    def test_heaviest_first_cycling_to_count(self):
        data = gif(6, 1, PAL4, [frame([1, 1, 1, 2, 2, 3], 6, 1)])
        self.assertEqual(sprite_colours.shown(data), [RED, GREEN, BLUE])
        self.assertEqual(sprite_colours.colours(data), [RED, GREEN, BLUE, RED, GREEN])
        self.assertEqual(sprite_colours.colours(data, 2), [RED, GREEN])

    def test_a_lively_colour_leads_black_and_grey(self):
        # Black 4, grey 3, red 2, blue 1: red leads, the rest keep their order.
        data = gif(10, 1, PAL8, [frame([0, 0, 0, 0, 4, 4, 4, 1, 1, 3], 10, 1)])
        self.assertEqual(sprite_colours.colours(data), [RED, BLACK, GREY, BLUE, RED])

    def test_greys_alone_keep_their_order(self):
        data = gif(3, 1, PAL8, [frame([4, 4, 0], 3, 1)])
        self.assertEqual(sprite_colours.colours(data, 2), [GREY, BLACK])

    def test_a_lively_colour_past_count_still_leads(self):
        # Black 2, grey 1, red 1 (grey before red: equal weights sort by value).
        data = gif(4, 1, PAL8, [frame([0, 0, 4, 1], 4, 1)])
        self.assertEqual(sprite_colours.shown(data), [RED, BLACK, GREY])
        self.assertEqual(sprite_colours.colours(data, 2), [RED, BLACK])

    def test_nothing_visible(self):
        data = gif(2, 1, PAL4, [frame([0, 0], 2, 1, transparent=0)])
        self.assertEqual(sprite_colours.colours(data), [])


class CLITest(unittest.TestCase):
    def run_main(self, args):
        out, err = io.StringIO(), io.StringIO()
        real = sys.stdout, sys.stderr
        sys.stdout, sys.stderr = out, err
        try:
            code = sprite_colours.main(args)
        finally:
            sys.stdout, sys.stderr = real
        return code, out.getvalue(), err.getvalue()

    def test_line_count_and_errors(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "a.gif")
            with open(path, "wb") as f:
                f.write(gif(3, 1, PAL4, [frame([3, 3, 1], 3, 1)]))
            self.assertEqual(self.run_main([path]), (0, "0000FF FF0000 0000FF FF0000 0000FF\n", ""))
            self.assertEqual(self.run_main(["--count", "1", path]), (0, "0000FF\n", ""))
            for args in (["--count", "0", path], ["--count", "²", path], ["--count", "x", path], [],
                         [path, path], [os.path.join(d, "none.gif")], [d]):
                code, out, err = self.run_main(args)
                self.assertEqual((code, out), (2, ""), args)
                self.assertNotIn("a.gif", err, args)
            for content in (b"not a gif", b"GIF89a\x03\x00\x01",
                            gif(2, 1, PAL4, [frame([0, 0], 2, 1, transparent=0)])):
                with open(path, "wb") as f:
                    f.write(content)
                code, out, err = self.run_main([path])
                self.assertEqual((code, out), (2, ""), content[:12])
                self.assertTrue(err.startswith("sprite_colours: "), err)
                self.assertNotIn("a.gif", err)


if __name__ == "__main__":
    unittest.main()
