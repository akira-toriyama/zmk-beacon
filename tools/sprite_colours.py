#!/usr/bin/env python3
"""The colours a sprite GIF shows most, for the key LEDs' palette (canon t-0967).

  sprite_colours.py [--count N] <front.gif>

Prints one line, RRGGBB per colour separated by spaces, the colour to light
the keys with first (canon's `dongle.py palette` takes the line's words as
slots 0..). Nothing it prints names the GIF. Standard library only, Python
3.9. Exit 0; 2 for bad usage or a file that is no GIF.

A colour's weight is the pixels that show it summed over the frames, each
frame counted for its delay as the device plays it (sprite_pack.play_delay):
what the eye sees most. A transparent pixel counts for nothing; black counts
(a sprite's outline is one of its colours, the user's call). A colour within
MERGE_DISTANCE of a heavier one joins it, heaviest first (80 in RGB: the
shades of one hue merge and two hues stay apart; 40 kept shades apart; seen
on ten of the user's 649 sprites, 2026-10-19), and the heaviest COUNT remain.
The first printed is the heaviest lively colour, saturation and value both at
least LIVELY (a quarter): slot 0 is painted as hue and saturation, and a
black or grey slot 0 paints white. 198 of the 649 sprites show black or grey
most, and 195 of those have a lively colour among their COUNT; a sprite of
greys keeps its heaviest first (2026-10-19).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gifpics  # noqa: E402
from sprite_pack import play_delay  # noqa: E402

COUNT = 5
MERGE_DISTANCE = 80
LIVELY = 0.25


def weights(anim):
    """{(r, g, b): the pixels showing it, each frame weighted by its delay}."""
    counts = []
    for pic in anim.pictures:
        c = [0] * 256
        for v in pic:
            c[v] += 1
        counts.append(c)
    w = {}
    for pic, delay in anim.order:
        d = play_delay(delay)
        c = counts[pic]
        for v in range(1, len(anim.palette) + 1):
            if c[v]:
                rgb = anim.palette[v - 1]
                w[rgb] = w.get(rgb, 0) + c[v] * d
    return w


def distance(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5


def merge(w, max_distance=MERGE_DISTANCE):
    """[(colour, weight)] heaviest first: a colour within max_distance of a
    heavier survivor adds its weight to that one and goes."""
    survivors = []
    for rgb, weight in sorted(w.items(), key=lambda kv: (-kv[1], kv[0])):
        for s in survivors:
            if distance(rgb, s[0]) <= max_distance:
                s[1] += weight
                break
        else:
            survivors.append([rgb, weight])
    survivors.sort(key=lambda s: (-s[1], s[0]))
    return [(rgb, weight) for rgb, weight in survivors]


def lively(rgb, least=LIVELY):
    """Saturation and value (HSV) both at least `least`."""
    mx, mn = max(rgb), min(rgb)
    return mx >= 255 * least and mx > 0 and (mx - mn) >= mx * least


def colours(data, count=COUNT):
    """The colours of GIF file contents, the one for slot 0 first, at most count."""
    if count < 1:
        raise ValueError("count")
    top = [rgb for rgb, _ in merge(weights(gifpics.animation(data)))[:count]]
    for i, rgb in enumerate(top):
        if lively(rgb):
            return [rgb] + top[:i] + top[i + 1:]
    return top


def main(argv):
    count = COUNT
    args = list(argv)
    if len(args) >= 2 and args[0] == "--count":
        if not args[1].isdigit() or int(args[1]) < 1:
            print("sprite_colours: --count takes a whole number from 1", file=sys.stderr)
            return 2
        count = int(args[1])
        args = args[2:]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        with open(args[0], "rb") as f:
            data = f.read()
        found = colours(data, count)
    except OSError as e:
        print("sprite_colours: cannot read the GIF: %s" % (e.strerror or "error"), file=sys.stderr)
        return 2
    except gifpics.GifError as e:
        print("sprite_colours: %s" % e, file=sys.stderr)
        return 2
    print(" ".join("%02X%02X%02X" % rgb for rgb in found))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
