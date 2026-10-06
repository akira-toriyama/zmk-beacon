# The sprite pack

A sprite pack (`.spk`) holds a sprite animation as the distinct pictures it
shows and the order it shows them in, for the Prospector Dongle to play from
flash: embedded at build time or, later, received over Bluetooth into a flash
room and swapped without a reboot. `tools/sprite_pack.py` writes one from a
GIF (and a second GIF for the back view), checks one against its GIFs, and
prints one's sizes; its docstring is the byte layout. This page is the why.

## Why not the GIF itself

The Prospector Dongle played the GIF file as it was (LVGL's gifdec) until the
pack replaced it. Three things made the GIF the wrong unit for a sprite that
changes daily over the air:

- A GIF frame is a patch over the previous frame, so playback only moves
  forward: catching up after a slow render decodes every frame in between, and
  a key press cannot step to "the next different picture" without decoding
  through the same ones.
- A sprite animation repeats its pictures (a breathing cycle goes A B C B A),
  and a GIF stores every repeat again. On the user's collection of 649 front
  and back pairs, 105,673 frames showed only 36,015 distinct pictures.
- gifdec keeps 5 bytes a pixel plus a 16 KiB LZW cache in the LVGL pool: a
  153x94 sprite needed about 98 KB of a 90,112 byte pool and could not be
  built.

## What a pack stores

- One or two animations, front first; a name of up to 15 printable ASCII
  characters (what the HP bar shows); the total length and a CRC-32, so a
  receiver can tell a whole, intact file from a cut or corrupted one before
  opening it.
- Per animation: the canvas size, a palette of up to 255 RGB colours, the
  distinct pictures, and the frames as (picture, delay) pairs. Every picture
  is the whole composited canvas as a browser would show it: pixel value 0 is
  transparent and 1..n index the palette. Pixels are packed 4 to a byte pair
  when the palette has 15 colours or fewer (most sprites), 8 bits otherwise.
- A picture is a raw deflate stream of its band of rows (the rows above and
  below are transparent); picture 0 is the whole canvas and the preset
  dictionary of every other picture. Any picture therefore decodes in one step
  from the file and the decoded picture 0, in any order, into one canvas
  buffer of stride x height bytes. The device keeps the canvas and the
  dictionary: 2 x stride x height bytes, 14,476 for the 153x94 sprite.
- A frame's delay is in 10 ms units as the device plays it: the converter
  turns 0 and 1 into 10 (100 ms), as browsers do, so the player applies no
  rule of its own.

## Why deflate with a dictionary

Measured on the user's 1,298 GIFs (57.6 MB; 2026-10-06), the picture data
plus tables came to:

| encoding of a picture | of the GIFs' size | largest pair |
| --- | --- | --- |
| GIF's LZW, cropped to the picture's box | 40.9% | 260 KiB |
| deflate, cropped, 4 or 8 bits a pixel | 39.1% | |
| deflate of the row band, picture 0 as the dictionary | 27.5% | 204 KiB |

The dictionary pays because the pictures of one animation are mostly the same
picture shifted by a pixel or two, which LZ77 matches find; 4 bits a pixel
pays because a match then covers twice the pixels. A better dictionary than
picture 0 (the best of five candidates, or of all pictures) would save another
2-4%, not worth a rule. zlib does the encoding, so the converter is standard
library only and runs on macOS's `/usr/bin/python3` (3.9) and in the ZMK build
image; the device's inflater (`src/sprite_pack.c`, once the player uses it)
is about 400 lines and checks every write against the band, so erased or
foreign flash decodes to an error.

## Correctness

- `gifpics.py` composes frames as browsers do (disposal 0-3, transparency,
  interlace, local colour tables, clipping, cut files) and was compared frame
  by frame with Pillow 12.3 on the 1,298 GIFs: every pixel and every delay
  matched (2026-10-06).
- `sprite_pack.py check` unpacks a file exactly as the device decodes it and
  compares every frame with the GIFs; the same 1,298 round-tripped with no
  mismatch, and the device's C inflater decoded all of them to the same bytes
  on the host.
- `tools/test_sprite_pack.py` covers the same rules on GIFs it writes itself
  (`python3 -m unittest discover tools`); the GIFs above are personal files
  and never enter the repository or CI.
