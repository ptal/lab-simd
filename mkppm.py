#!/usr/bin/env python3
"""Generate a binary PPM (P6) image of a given size from a template image.

Usage: ./mkppm.py [template.ppm] [size in MiB] [-o out.ppm] [--width N]

The template is tiled, so the output keeps real image content (and its mix of
saturated and mid-range channels) instead of the uniform noise a random fill
would give. The aspect ratio of the template is preserved unless --width says
otherwise.

Sizes are the pixel data, 3 bytes per pixel, in MiB (1024 x 1024) so they match
the "MB in memory" line brightness.cpp prints for uint8_t channels. The 15-byte
header and the rounding to a whole number of rows make the file a hair larger.

  ./mkppm.py small.ppm 64 -o large.ppm
"""

import argparse
import math
import re
import sys

BYTES_PER_PIXEL = 3
MIB = 1024 * 1024


def read_ppm(path):
    """Width, height and the pixel data of a binary PPM, as (w, h, bytes)."""
    with open(path, "rb") as handle:
        data = handle.read()
    # The three header fields may be separated by any whitespace and preceded by
    # comment lines, and exactly one whitespace character follows the maxval.
    header = re.match(rb"(P6)\s+(?:#[^\n]*\n\s*)*(\d+)\s+(?:#[^\n]*\n\s*)*(\d+)"
                      rb"\s+(?:#[^\n]*\n\s*)*(\d+)\s", data)
    if not header:
        sys.exit(f"{path}: not a binary PPM (P6) image")
    width, height, maxval = (int(header.group(i)) for i in (2, 3, 4))
    if maxval != 255:
        sys.exit(f"{path}: only 255 as maximum value is supported")
    pixels = data[header.end():]
    expected = width * height * BYTES_PER_PIXEL
    if len(pixels) < expected:
        sys.exit(f"{path}: truncated, {len(pixels)} pixel bytes for {expected} expected")
    return width, height, pixels[:expected]


def target_shape(width, height, wanted, forced_width):
    """The output dimensions holding `wanted` pixels, as close to the template's
    aspect ratio as a whole number of rows allows."""
    if forced_width:
        out_width = forced_width
    else:
        out_width = max(1, round(width * math.sqrt(wanted / (width * height))))
    return out_width, max(1, round(wanted / out_width))


def write_ppm(path, out_width, out_height, width, height, pixels):
    """Tile the template into `path`, one output row at a time.

    Only one band of `height` output rows is ever built: row y of the output is
    row y % height of the template repeated across, so the band can be reused
    for every band below it."""
    row_bytes = width * BYTES_PER_PIXEL
    copies = math.ceil(out_width / width)
    band = [pixels[y * row_bytes:(y + 1) * row_bytes] * copies for y in range(height)]
    band = [row[:out_width * BYTES_PER_PIXEL] for row in band]
    with open(path, "wb") as handle:
        handle.write(f"P6\n{out_width} {out_height}\n255\n".encode())
        for y in range(out_height):
            handle.write(band[y % height])


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("template", nargs="?", default="small.ppm")
    parser.add_argument("size", nargs="?", type=float, default=64,
                        help="size of the pixel data in MiB (default: 64)")
    parser.add_argument("-o", "--output", default="large.ppm")
    parser.add_argument("--width", type=int, default=None,
                        help="force this width instead of keeping the aspect ratio")
    args = parser.parse_args()

    if args.size <= 0:
        sys.exit("the size must be positive")
    if args.width is not None and args.width <= 0:
        sys.exit("the width must be positive")

    width, height, pixels = read_ppm(args.template)
    wanted = args.size * MIB / BYTES_PER_PIXEL
    out_width, out_height = target_shape(width, height, wanted, args.width)
    write_ppm(args.output, out_width, out_height, width, height, pixels)

    written = out_width * out_height * BYTES_PER_PIXEL
    if out_width < width or out_height < height:
        source = "cropped from"
    else:
        source = f"{out_width / width:.1f}x{out_height / height:.1f} tiles of"
    print(f"{args.output}: {out_width}x{out_height} pixels, {written / MIB:.2f} MiB "
          f"of pixel data, {source} {args.template} ({width}x{height})")


if __name__ == "__main__":
    main()
