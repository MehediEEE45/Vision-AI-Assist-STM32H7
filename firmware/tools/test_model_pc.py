"""Run the firmware's AI pipeline on a PC to check a model before flashing it.

Mirrors src/ai/detector.cpp: grayscale 320x240 frame -> left / centre / right 160x160 crops
-> bilinear resize to the model input -> [-1, 1] normalisation -> int8 quantisation -> softmax.

Usage:
    pip install ai-edge-litert pillow numpy
    python tools/test_model_pc.py models/person_detect.tflite photo.jpg [more.jpg ...]
    python tools/test_model_pc.py models/my_model.tflite img.jpg --labels nothing person door stairs
"""
import argparse

import numpy as np
from PIL import Image
from ai_edge_litert.interpreter import Interpreter

FRAME_W, FRAME_H = 320, 240           # CAM_AI_WIDTH x CAM_AI_HEIGHT in include/config.h
CROP = 160                            # AI_CROP_SIZE in include/config.h
SIDES = ["left", "centre", "right"]


def load_interpreter(path):
    """Desktop LiteRT rejects per-tensor quantization that names a non-zero axis (an old
    converter quirk in person_detect.tflite; TFLite Micro accepts it). Patch a copy in memory."""
    data = bytearray(open(path, "rb").read())
    try:
        return Interpreter(model_content=bytes(data))
    except ValueError:
        import tflite  # pip install tflite
        model = tflite.Model.GetRootAsModel(data, 0)
        for s in range(model.SubgraphsLength()):
            g = model.Subgraphs(s)
            for i in range(g.TensorsLength()):
                tensor = g.Tensors(i)
                q = tensor.Quantization()
                if q is None or tensor.ShapeLength() != 1:   # only 1-D (bias) tensors are affected
                    continue
                o = q._tab.Offset(16)  # vtable slot of quantized_dimension
                if o:
                    pos = q._tab.Pos + o
                    data[pos:pos + 4] = (0).to_bytes(4, "little")
        return Interpreter(model_content=bytes(data))


def to_frame(path):
    """Load an image the way the camera delivers it: grayscale, 4:3, 320x240."""
    im = Image.open(path).convert("L")
    w, h = im.size
    target = 4 / 3
    if w / h > target:                       # too wide: crop sides
        nw = int(h * target); im = im.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
    else:                                    # too tall: crop top/bottom
        nh = int(w / target); im = im.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))
    return np.asarray(im.resize((FRAME_W, FRAME_H), Image.BILINEAR), dtype=np.float32)


def bilinear_crop(frame, x0, y0, side, size):
    """Same sampling as fillInput() in detector.cpp."""
    out = np.zeros((size, size), np.float32)
    scale = side / size
    for y in range(size):
        fy = (y + 0.5) * scale - 0.5; iy = int(fy); dy = fy - iy
        if iy < 0: iy, dy = 0, 0.0
        if iy >= side - 1: iy, dy = side - 2, 1.0
        for x in range(size):
            fx = (x + 0.5) * scale - 0.5; ix = int(fx); dx = fx - ix
            if ix < 0: ix, dx = 0, 0.0
            if ix >= side - 1: ix, dx = side - 2, 1.0
            r0 = frame[y0 + iy, x0 + ix:x0 + ix + 2]
            r1 = frame[y0 + iy + 1, x0 + ix:x0 + ix + 2]
            top = r0[0] + (r0[1] - r0[0]) * dx
            bot = r1[0] + (r1[1] - r1[0]) * dx
            out[y, x] = top + (bot - top) * dy
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("images", nargs="+")
    ap.add_argument("--labels", nargs="*", default=["nothing", "person"])
    a = ap.parse_args()

    it = load_interpreter(a.model)
    it.allocate_tensors()
    inp, out = it.get_input_details()[0], it.get_output_details()[0]
    size = inp["shape"][1]
    in_scale, in_zp = inp["quantization"]
    out_scale, out_zp = out["quantization"]
    print(f"model input {inp['shape']} {inp['dtype'].__name__} scale={in_scale:.6f} zp={in_zp}")

    for path in a.images:
        frame = to_frame(path)
        side = CROP
        xs = [0, (FRAME_W - side) // 2, FRAME_W - side]
        y0 = (FRAME_H - side) // 2
        results = []
        for name, x0 in zip(SIDES, xs):
            crop = bilinear_crop(frame, x0, y0, side, size)
            xn = crop / 127.5 - 1.0
            q = np.floor(xn / in_scale + 0.5) + in_zp
            lo, hi = (-128, 127) if inp["dtype"] == np.int8 else (0, 255)
            q = np.clip(q, lo, hi).astype(inp["dtype"]).reshape(inp["shape"])
            it.set_tensor(inp["index"], q)
            it.invoke()
            raw = it.get_tensor(out["index"]).astype(np.float32).ravel()
            prob = (raw - out_zp) * out_scale if out_scale else raw
            best = int(np.argmax(prob))
            label = a.labels[best] if best < len(a.labels) else f"class{best}"
            results.append(f"{name}: {label} {prob[best]:.2f}")
        print(f"{path}\n   " + "   ".join(results))


if __name__ == "__main__":
    main()
