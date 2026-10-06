"""A GIF as the distinct pictures it shows and the order it shows them in.

A picture is the whole composited canvas as a browser shows it for one frame:
one byte a pixel, 0 for transparent, 1..n for palette[0..n-1]. Every pass of
the animation starts from a transparent canvas, so the pictures and their
order describe the playback completely (sprite_pack.py stores them).

Standard library only, Python 3.9 (macOS's /usr/bin/python3 and the ZMK build
image both run it). The composition follows what browsers and Pillow do, and
was checked against Pillow frame by frame on 1,298 GIFs (2026-10-06):

- A graphic control extension serves the one image after it; an image without
  one has no transparency, delay 0 and disposal 0.
- Disposal 2 clears the frame's rectangle to transparent, 3 restores the
  canvas from before the frame; 0 and 1 leave it.
- The logical screen's background colour is never painted.
- A frame's rectangle is clipped to the canvas; a short pixel stream is padded
  with the transparent index (or 0) and a long one cut.
- A file that ends early plays what was complete; one with no frame is an error.
- A colour index past the active colour table is black.
"""


class GifError(ValueError):
    pass


_OPAQUE = bytes(0 if i == 0 else 0xFF for i in range(256))
_INTERLACE_PASSES = ((0, 8), (4, 8), (2, 4), (1, 2))


def _lzw_decode(min_size, data, npix):
    if not 1 <= min_size <= 11:
        raise GifError("LZW minimum code size %d" % min_size)
    clear = 1 << min_size
    eoi = clear + 1
    code_size = min_size + 1
    mask = (1 << code_size) - 1
    table = [bytes([i]) for i in range(clear)] + [b"", b""]
    out = []
    total = 0
    bitbuf = 0
    bits = 0
    prev = None
    for byte in data:
        bitbuf |= byte << bits
        bits += 8
        while bits >= code_size:
            code = bitbuf & mask
            bitbuf >>= code_size
            bits -= code_size
            if code == clear:
                del table[clear + 2:]
                code_size = min_size + 1
                mask = (1 << code_size) - 1
                prev = None
                continue
            if code == eoi:
                return b"".join(out)
            if prev is None:
                if code >= clear:
                    raise GifError("LZW code %d before any literal" % code)
                entry = table[code]
            elif code < len(table):
                entry = table[code]
                if len(table) < 4096:
                    table.append(prev + entry[:1])
            elif code == len(table) and len(table) < 4096:
                entry = prev + prev[:1]
                table.append(entry)
            else:
                raise GifError("LZW code %d beyond the table" % code)
            out.append(entry)
            total += len(entry)
            if total >= npix:
                return b"".join(out)
            prev = entry
            if len(table) == (1 << code_size) and code_size < 12:
                code_size += 1
                mask = (1 << code_size) - 1
    return b"".join(out)


def _sub_blocks(b, pos):
    chunks = []
    while True:
        n = b[pos]
        pos += 1
        if n == 0:
            return b"".join(chunks), pos
        chunks.append(b[pos:pos + n])
        pos += n


def read_frames(b):
    """(width, height, frames): the image descriptors with their control data."""
    if b[:6] not in (b"GIF89a", b"GIF87a"):
        raise GifError("not a GIF")
    width = b[6] | b[7] << 8
    height = b[8] | b[9] << 8
    if width == 0 or height == 0:
        raise GifError("empty canvas")
    flags = b[10]
    pos = 13
    gct = b""
    if flags & 0x80:
        n = 3 * (2 << (flags & 7))
        gct = b[pos:pos + n]
        pos += n
    frames = []
    control = None
    try:
        while True:
            kind = b[pos]
            if kind == 0x3B:
                break
            if kind == 0x21:
                label = b[pos + 1]
                body, pos = _sub_blocks(b, pos + 2)
                if label == 0xF9 and len(body) >= 4:
                    control = ((body[0] >> 2) & 7, body[1] | body[2] << 8,
                               body[3] if body[0] & 1 else None)
                continue
            if kind != 0x2C:
                raise GifError("unknown block 0x%02x at %d" % (kind, pos))
            x = b[pos + 1] | b[pos + 2] << 8
            y = b[pos + 3] | b[pos + 4] << 8
            w = b[pos + 5] | b[pos + 6] << 8
            h = b[pos + 7] | b[pos + 8] << 8
            iflags = b[pos + 9]
            pos += 10
            palette = gct
            if iflags & 0x80:
                n = 3 * (2 << (iflags & 7))
                palette = b[pos:pos + n]
                pos += n
            min_size = b[pos]
            data, pos = _sub_blocks(b, pos + 1)
            disposal, delay, transparent = control or (0, 0, None)
            control = None
            frames.append({
                "rect": (x, y, w, h), "interlaced": bool(iflags & 0x40),
                "palette": palette, "min_size": min_size, "data": data,
                "disposal": disposal, "delay": delay, "transparent": transparent,
            })
    except IndexError:
        pass
    if not frames:
        raise GifError("no frame")
    return width, height, frames


class Animation:
    def __init__(self, width, height, palette, pictures, order):
        self.width = width
        self.height = height
        # [(r, g, b)]: pixel value i + 1.
        self.palette = palette
        # [bytes(width * height)], in the order they first show.
        self.pictures = pictures
        # [(picture index, the frame's delay in 1/100 s as the file has it)].
        self.order = order


def animation(b):
    """The Animation of GIF file contents b; GifError when it is not one."""
    width, height, frames = read_frames(b)
    colours = {}
    canvas = bytearray(width * height)
    pictures = {}
    order = []
    for fr in frames:
        x, y, w, h = fr["rect"]
        px = _lzw_decode(fr["min_size"], fr["data"], w * h)
        if len(px) < w * h:
            px += bytes([0 if fr["transparent"] is None else fr["transparent"]]) * (w * h - len(px))
        elif len(px) > w * h:
            px = px[:w * h]

        palette = fr["palette"]
        to_value = bytearray(256)
        for i in sorted(set(px)):
            if i == fr["transparent"]:
                continue
            rgb = tuple(palette[3 * i:3 * i + 3])
            if len(rgb) < 3:
                rgb = (0, 0, 0)
            value = colours.setdefault(rgb, len(colours) + 1)
            if value > 255:
                raise GifError("more than 255 colours")
            to_value[i] = value
        px = px.translate(bytes(to_value))

        if fr["interlaced"]:
            rows = [None] * h
            i = 0
            for start, step in _INTERLACE_PASSES:
                for r in range(start, h, step):
                    rows[r] = px[i * w:(i + 1) * w]
                    i += 1
        else:
            rows = [px[r * w:(r + 1) * w] for r in range(h)]

        before = bytes(canvas) if fr["disposal"] == 3 else None
        cw = min(w, width - x)
        for r in range(min(h, height - y)):
            if cw <= 0:
                break
            row = rows[r][:cw]
            off = (y + r) * width + x
            if 0 in row:
                keep = int.from_bytes(row.translate(_OPAQUE), "big")
                src = int.from_bytes(row, "big")
                dst = int.from_bytes(canvas[off:off + cw], "big")
                row = ((src & keep) | (dst & ~keep & ((1 << (8 * cw)) - 1))).to_bytes(cw, "big")
            canvas[off:off + cw] = row

        shown = bytes(canvas)
        order.append((pictures.setdefault(shown, len(pictures)), fr["delay"]))

        if fr["disposal"] == 2:
            for r in range(min(h, height - y)):
                if cw <= 0:
                    break
                off = (y + r) * width + x
                canvas[off:off + cw] = bytes(cw)
        elif fr["disposal"] == 3:
            canvas[:] = before

    palette = [rgb for rgb, _ in sorted(colours.items(), key=lambda kv: kv[1])]
    return Animation(width, height, palette, list(pictures), order)
