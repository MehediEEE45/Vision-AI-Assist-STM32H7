"""Grab camera frames from the board over USB serial and show / save them.

The firmware answers the 'f' command with "FRAME <w> <h>\\n" followed by w*h grayscale bytes.

    pip install pyserial pillow
    python tools/frame_viewer.py COM5            # one frame, saved as frame_000.png and opened
    python tools/frame_viewer.py COM5 --count 20 # save 20 frames (dataset collection on the PC)
"""
import argparse
import time

import serial
from PIL import Image, ImageDraw

CROP = 160  # AI_CROP_SIZE in include/config.h


def read_frame(port):
    port.reset_input_buffer()
    port.write(b"f")
    deadline = time.time() + 5
    while time.time() < deadline:
        line = port.readline().decode(errors="ignore").strip()
        if line.startswith("FRAME"):
            _, w, h = line.split()
            w, h = int(w), int(h)
            data = port.read(w * h)
            if len(data) == w * h:
                return Image.frombytes("L", (w, h), data)
    raise TimeoutError("no frame received - is the camera working? check the serial log")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--overlay", action="store_true", help="draw the three AI crop squares")
    a = ap.parse_args()
    with serial.Serial(a.port, 115200, timeout=2) as port:
        for i in range(a.count):
            img = read_frame(port)
            if a.overlay:
                rgb = img.convert("RGB")
                d = ImageDraw.Draw(rgb)
                w, h = img.size
                y0 = (h - CROP) // 2
                for x0, col in [(0, "red"), ((w - CROP) // 2, "lime"), (w - CROP, "blue")]:
                    d.rectangle([x0, y0, x0 + CROP - 1, y0 + CROP - 1], outline=col)
                img = rgb
            name = f"frame_{i:03d}.png"
            img.save(name)
            print("saved", name)
            if a.count == 1:
                img.show()


if __name__ == "__main__":
    main()
