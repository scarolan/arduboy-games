#!/usr/bin/env python3
"""
img2arduboy.py - Convert any image to Arduboy 128x64 1-bit PROGMEM bitmap.

Usage:
    python img2arduboy.py input.jpg [--name SPRITE_NAME] [--threshold 128] [--dither atkinson|floyd|threshold] [--preview] [--invert]

Outputs:
    - PROGMEM C byte array to stdout (paste into .ino)
    - Optional preview image saved as input_preview.png
"""

import argparse
import sys
from pathlib import Path
from PIL import Image

ARDUBOY_W = 128
ARDUBOY_H = 64


def atkinson_dither(img):
    """Atkinson dithering - classic Mac look, good for noir."""
    pixels = list(img.getdata())
    w, h = img.size
    buf = [float(p) for p in pixels]

    for y in range(h):
        for x in range(w):
            i = y * w + x
            old = buf[i]
            new = 255.0 if old >= 128 else 0.0
            buf[i] = new
            err = (old - new) / 8.0  # Atkinson divides by 8, distributes 6/8

            # Distribute to 6 neighbors
            for dx, dy in [(1,0),(2,0),(-1,1),(0,1),(1,1),(0,2)]:
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    buf[ny * w + nx] += err

    return Image.frombytes('L', (w, h), bytes(max(0, min(255, int(v))) for v in buf))


def floyd_steinberg_dither(img):
    """Floyd-Steinberg dithering - smoother gradients."""
    pixels = list(img.getdata())
    w, h = img.size
    buf = [float(p) for p in pixels]

    for y in range(h):
        for x in range(w):
            i = y * w + x
            old = buf[i]
            new = 255.0 if old >= 128 else 0.0
            buf[i] = new
            err = old - new

            for dx, dy, weight in [(1,0,7/16),(-1,1,3/16),(0,1,5/16),(1,1,1/16)]:
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    buf[ny * w + nx] += err * weight

    return Image.frombytes('L', (w, h), bytes(max(0, min(255, int(v))) for v in buf))


def threshold_convert(img, thresh):
    """Simple threshold - no dithering."""
    return img.point(lambda p: 255 if p >= thresh else 0)


def image_to_progmem(img_1bit, name):
    """Convert 1-bit image to Arduboy PROGMEM byte array.

    Arduboy bitmap format: column-major, each byte = 8 vertical pixels,
    LSB is top pixel. Pages of 8 rows each.
    """
    w, h = img_1bit.size
    assert w == ARDUBOY_W and h == ARDUBOY_H

    pixels = img_1bit.load()
    data = []

    for page in range(h // 8):
        for x in range(w):
            byte = 0
            for bit in range(8):
                y = page * 8 + bit
                if pixels[x, y] > 128:
                    byte |= (1 << bit)
            data.append(byte)

    # Format as C array
    lines = [f"const uint8_t PROGMEM {name}[] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"  {hex_vals},")
    lines.append("};")
    lines.append(f"// {name}: {w}x{h}, {len(data)} bytes")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Convert image to Arduboy PROGMEM bitmap")
    parser.add_argument("input", help="Input image file")
    parser.add_argument("--name", default="bitmap", help="C variable name (default: bitmap)")
    parser.add_argument("--threshold", type=int, default=128, help="Threshold for simple mode (0-255)")
    parser.add_argument("--dither", choices=["atkinson", "floyd", "threshold"], default="atkinson",
                       help="Dithering algorithm (default: atkinson)")
    parser.add_argument("--preview", action="store_true", help="Save preview PNG")
    parser.add_argument("--invert", action="store_true", help="Invert black/white")
    args = parser.parse_args()

    # Load and resize
    img = Image.open(args.input).convert("L")
    img = img.resize((ARDUBOY_W, ARDUBOY_H), Image.LANCZOS)

    # Apply contrast enhancement before dithering
    # Stretch histogram to full range
    min_val = min(img.getdata())
    max_val = max(img.getdata())
    if max_val > min_val:
        img = img.point(lambda p: int((p - min_val) * 255.0 / (max_val - min_val)))

    # Dither / threshold
    if args.dither == "atkinson":
        result = atkinson_dither(img)
    elif args.dither == "floyd":
        result = floyd_steinberg_dither(img)
    else:
        result = threshold_convert(img, args.threshold)

    # Convert to pure 1-bit
    result = result.point(lambda p: 255 if p >= 128 else 0)

    if args.invert:
        result = result.point(lambda p: 255 - p)

    # Save preview
    if args.preview:
        preview_path = Path(args.input).with_name(Path(args.input).stem + "_preview.png")
        # Save at 4x scale for easy viewing
        preview = result.resize((ARDUBOY_W * 4, ARDUBOY_H * 4), Image.NEAREST)
        preview.save(preview_path)
        print(f"// Preview saved to: {preview_path}", file=sys.stderr)

    # Output PROGMEM
    print(image_to_progmem(result, args.name))


if __name__ == "__main__":
    main()
