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
GREY, DARK_RED, WHITE = (128, 128, 128), (200, 20, 20), (255, 255, 255)
PAL8 = [BLACK, RED, GREEN, BLUE, GREY, DARK_RED, WHITE, (0, 0, 0)]
DIM, DEEP_RED = (20, 20, 20), (60, 0, 0)


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

    def test_dark(self):
        self.assertTrue(sprite_colours.dark(BLACK))
        self.assertTrue(sprite_colours.dark(DIM))
        self.assertTrue(sprite_colours.dark((50, 50, 50)), "a shade under a fifth")
        self.assertFalse(sprite_colours.dark((51, 0, 0)), "a fifth, just")
        self.assertFalse(sprite_colours.dark(DEEP_RED))
        self.assertFalse(sprite_colours.dark(GREY))


class ColoursTest(unittest.TestCase):
    def test_heaviest_first_cycling_to_count(self):
        data = gif(6, 1, PAL4, [frame([1, 1, 1, 2, 2, 3], 6, 1)])
        self.assertEqual(sprite_colours.shown([data]), [RED, GREEN, BLUE])
        self.assertEqual(sprite_colours.colours([data]), [RED, GREEN, BLUE, RED, GREEN])
        self.assertEqual(sprite_colours.colours([data], 2), [RED, GREEN])

    def test_a_lively_colour_leads_grey_and_black_goes(self):
        # Black 4, grey 3, red 2, blue 1: black is dark and goes, red leads,
        # the rest keep their order.
        data = gif(10, 1, PAL8, [frame([0, 0, 0, 0, 4, 4, 4, 1, 1, 3], 10, 1)])
        self.assertEqual(sprite_colours.colours([data]), [RED, GREY, BLUE, RED, GREY])

    def test_greys_alone_keep_their_order(self):
        data = gif(3, 1, PAL8, [frame([4, 4, 6], 3, 1)])
        self.assertEqual(sprite_colours.colours([data], 2), [GREY, WHITE])

    def test_a_lively_colour_past_count_still_leads(self):
        # Black 2 (goes), grey 2, white 2, red 1 (grey before white: equal
        # weights sort by value).
        data = gif(7, 1, PAL8, [frame([0, 0, 4, 4, 6, 6, 1], 7, 1)])
        self.assertEqual(sprite_colours.shown([data]), [RED, GREY, WHITE])
        self.assertEqual(sprite_colours.colours([data], 2), [RED, GREY])

    def test_dark_counts_for_nothing_before_merging(self):
        # Black 3, deep red 1, green 1: deep red is within MERGE_DISTANCE of
        # black and would go with it merged first; left out before, black
        # takes nothing, and deep red stays (green leads: equal weights sort
        # by value, and green is lively).
        pal = [BLACK, GREEN, DEEP_RED, BLUE]
        data = gif(5, 1, pal, [frame([0, 0, 0, 1, 2], 5, 1)])
        self.assertEqual(sprite_colours.shown([data]), [GREEN, DEEP_RED])

    def test_all_dark_keeps_them(self):
        # Black 2, dim 1: nothing else to show, so both count, and dim merges
        # into black.
        pal = [BLACK, DIM, GREY, WHITE]
        data = gif(3, 1, pal, [frame([0, 0, 1], 3, 1)])
        self.assertEqual(sprite_colours.shown([data]), [BLACK])
        self.assertEqual(sprite_colours.colours([data], 2), [BLACK, BLACK])
        # A dark view beside a lit one adds nothing, like an empty one.
        lit = gif(2, 1, PAL4, [frame([1, 1], 2, 1)])
        self.assertEqual(sprite_colours.shown([lit, data]), [RED])
        self.assertEqual(sprite_colours.shown([data, lit]), [RED])

    def test_nothing_visible(self):
        data = gif(2, 1, PAL4, [frame([0, 0], 2, 1, transparent=0)])
        self.assertEqual(sprite_colours.colours([data]), [])

    def test_two_views_count_the_same(self):
        # Front: red 3, green 1 (delay 0 plays as 10): shares 3/4 and 1/4.
        # Back: blue 1, green 3 at delay 100: weights 100 and 300, ten times
        # the front's, but the same shares. Together green 1, red 3/4, blue
        # 1/4; summed as they are, blue (100) would lead red (30).
        front = gif(4, 1, PAL4, [frame([1, 1, 1, 2], 4, 1)])
        back = gif(4, 1, PAL4, [frame([3, 2, 2, 2], 4, 1, delay=100)])
        self.assertEqual(sprite_colours.shown([front, back]), [GREEN, RED, BLUE])
        self.assertEqual(sprite_colours.shown([back, front]), [GREEN, RED, BLUE], "in either order")
        self.assertEqual(sprite_colours.combined([gifpics.animation(front), gifpics.animation(back)]),
                         {RED: 30 * 400, GREEN: 10 * 400 + 300 * 40, BLUE: 100 * 40})

    def test_a_view_that_shows_nothing_adds_nothing(self):
        front = gif(4, 1, PAL4, [frame([1, 1, 1, 2], 4, 1)])
        empty = gif(2, 1, PAL4, [frame([0, 0], 2, 1, transparent=0)])
        self.assertEqual(sprite_colours.shown([front, empty]), sprite_colours.shown([front]))
        self.assertEqual(sprite_colours.colours([empty, empty]), [])


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
            back = os.path.join(d, "b.gif")
            with open(back, "wb") as f:
                f.write(gif(2, 1, PAL4, [frame([2, 2], 2, 1)]))
            self.assertEqual(self.run_main([path]), (0, "0000FF FF0000 0000FF FF0000 0000FF\n", ""))
            self.assertEqual(self.run_main(["--count", "1", path]), (0, "0000FF\n", ""))
            # The back's two green pixels weigh as much as the front's three: green leads blue.
            self.assertEqual(self.run_main([path, back]), (0, "00FF00 0000FF FF0000 00FF00 0000FF\n", ""))
            self.assertEqual(self.run_main(["--count", "2", path, back]), (0, "00FF00 0000FF\n", ""))
            for args in (["--count", "0", path], ["--count", "²", path], ["--count", "x", path], [],
                         [os.path.join(d, "none.gif")], [path, os.path.join(d, "none.gif")], [d], [path, d]):
                code, out, err = self.run_main(args)
                self.assertEqual((code, out), (2, ""), args)
                self.assertNotIn("a.gif", err, args)
                self.assertNotIn("b.gif", err, args)
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
