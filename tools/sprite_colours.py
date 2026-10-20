#!/usr/bin/env python3
"""The colours a sprite shows most, for the key LEDs' palette (canon t-0967).

  sprite_colours.py [--count N] <front.gif> [<back.gif> ...]

Prints one line of COUNT (or N) words, RRGGBB each, the colour to light the
keys with first: canon's `dongle.py palette` takes the line's words as
slots 0.. (its t-0967). Nothing it prints names a GIF. Standard library
only, Python 3.9. Exit 0; 2 for bad usage, a file that is no GIF, or GIFs
that show nothing.

The GIFs are one sprite's views (its front, and its back, which the
Prospector Dongle shows while a modifier is held) and count the same: in
each, a colour's weight is the pixels that show it summed over the frames,
each frame counted for its delay as the device plays it
(sprite_pack.play_delay), what the eye sees most; the views' weights are
then scaled to one total and added, so a view with a longer loop or a
bigger figure does not outweigh the other (the user's call, 2026-10-20:
the colours of both views; a view that shows nothing adds nothing). A
transparent pixel counts for nothing; black counts (a sprite's outline is
one of its colours, the user's call). A colour within MERGE_DISTANCE of a
heavier one joins it, heaviest first (80 in RGB: the shades of one hue
merge and two hues stay apart; 40 kept shades apart; seen on ten of the
user's 649 sprites, 2026-10-19). The first word is the heaviest lively
colour, saturation and value both at least LIVELY (a quarter): slot 0 is
the colour a solid keyboard shows, and a black or grey one paints white.
The rest follow heaviest first, and the line cycles through them when the
sprite shows fewer than COUNT, so that every slot is one of its colours.
On the user's 649 fronts alone (2026-10-19): 198 show black or grey most,
195 of them have a lively colour and get it first, 3 of greys keep their
heaviest first; 18 show fewer than five colours. With the backs counted
too (every front has one, 2026-10-20): slot 0 stays the same colour on 306,
turns into a shade of it (within MERGE_DISTANCE) on 215 and into another
colour on 128; 148 get a colour among their five that the front alone did
not show; 15 show fewer than five colours.
"""
import os
import re
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


def combined(anims):
    """The weights of the animations added, each scaled so that every
    animation totals the same (the product of their totals: exact); one that
    shows nothing adds nothing."""
    per = [w for w in (weights(a) for a in anims) if w]
    totals = [sum(w.values()) for w in per]
    out = {}
    for i, w in enumerate(per):
        scale = 1
        for j, total in enumerate(totals):
            if j != i:
                scale *= total
        for rgb, weight in w.items():
            out[rgb] = out.get(rgb, 0) + weight * scale
    return out


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


def shown(datas):
    """The merged colours of one sprite's GIFs (their file contents),
    heaviest first, the heaviest lively one moved to the front; [] when
    nothing is visible."""
    top = [rgb for rgb, _ in merge(combined(gifpics.animation(d) for d in datas))]
    for i, rgb in enumerate(top):
        if lively(rgb):
            return [rgb] + top[:i] + top[i + 1:]
    return top


def colours(datas, count=COUNT):
    """count colours for slots 0.., cycling through shown(datas); [] when
    nothing is visible."""
    if count < 1:
        raise ValueError("count")
    top = shown(datas)
    return [top[i % len(top)] for i in range(count)] if top else []


def main(argv):
    count = COUNT
    args = list(argv)
    if len(args) >= 2 and args[0] == "--count":
        if not re.fullmatch("[0-9]+", args[1]) or int(args[1]) < 1:
            print("sprite_colours: --count takes a whole number from 1", file=sys.stderr)
            return 2
        count = int(args[1])
        args = args[2:]
    if not args:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        datas = []
        for path in args:
            with open(path, "rb") as f:
                datas.append(f.read())
        found = colours(datas, count)
    except OSError as e:
        print("sprite_colours: cannot read a GIF: %s" % (e.strerror or "error"), file=sys.stderr)
        return 2
    except gifpics.GifError as e:
        print("sprite_colours: %s" % e, file=sys.stderr)
        return 2
    if not found:
        print("sprite_colours: the GIFs show no colour", file=sys.stderr)
        return 2
    print(" ".join("%02X%02X%02X" % rgb for rgb in found))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
