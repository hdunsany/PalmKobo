#!/usr/bin/env python3
# palmkobo - extract CloudpilotEmu's silkscreen artwork into an 8-bit PGM.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: silkscreen2pgm.py <cloudpilot>/src/cloudpilot/native/Silkscreen.cpp out.pgm
#
# CloudpilotEmu embeds the Graffiti silkscreen as a PNG byte array (480x180, i.e. 3x
# the Palm's 160x60). We decode it, flatten onto white and convert to grayscale.
# Needs Pillow (pip install pillow) on the build machine only.

import io
import re
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__ or "usage: silkscreen2pgm.py Silkscreen.cpp out.pgm", file=sys.stderr)
        return 2

    src, dst = sys.argv[1], sys.argv[2]
    text = open(src, encoding="utf-8").read()

    body = re.search(r"silkscreenPng_data\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not body:
        print(f"{src}: no silkscreenPng_data array found", file=sys.stderr)
        return 1
    data = bytes(int(tok, 16) for tok in re.findall(r"0x([0-9a-fA-F]{2})", body.group(1)))

    try:
        from PIL import Image
    except ImportError:
        print("Pillow is required: pip install pillow", file=sys.stderr)
        return 1

    img = Image.open(io.BytesIO(data)).convert("RGBA")
    white = Image.new("RGBA", img.size, (255, 255, 255, 255))
    gray = Image.alpha_composite(white, img).convert("L")

    with open(dst, "wb") as f:
        f.write(b"P5\n%d %d\n255\n" % gray.size)
        f.write(gray.tobytes())

    print(f"wrote {dst} ({gray.size[0]}x{gray.size[1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
