"""Generate TinyEngine's original, deterministic Radiance HDR test environment."""

import math
from pathlib import Path


def rgbe(rgb):
    peak = max(rgb)
    if peak < 1e-32:
        return bytes(4)
    mantissa, exponent = math.frexp(peak)
    scale = mantissa * 256 / peak
    return bytes([min(255, int(channel * scale)) for channel in rgb] + [exponent + 128])


def main():
    width, height = 64, 32
    output = Path(__file__).resolve().parents[1] / "assets/environments/studio.hdr"
    output.parent.mkdir(parents=True, exist_ok=True)
    pixels = bytearray()
    for y in range(height):
        v = (y + 0.5) / height
        for x in range(width):
            u = (x + 0.5) / width
            ceiling = max(0, math.cos(v * math.pi))
            window = math.exp(-((u - 0.25) / 0.09) ** 8 - ((v - 0.4) / 0.16) ** 8)
            rim = math.exp(-((u - 0.75) / 0.045) ** 2 - ((v - 0.35) / 0.09) ** 2)
            pixels.extend(rgbe((0.35 + ceiling * 0.4 + window * 7 + rim,
                                0.45 + ceiling * 0.5 + window * 6 + rim * 2,
                                0.65 + ceiling * 0.7 + window * 4 + rim * 5)))
    header = f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n"
    output.write_bytes(header.encode("ascii") + pixels)


if __name__ == "__main__":
    main()
