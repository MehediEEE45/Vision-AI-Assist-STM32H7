"""Generate the voice prompts the firmware plays (16 kHz, 16-bit, mono WAV).

Copy the output folder to the microSD card as /prompts/ (e.g. E:\\prompts\\person.wav).

English (offline, uses the Windows / macOS / espeak voice):
    pip install pyttsx3 numpy
    python tools/make_prompts.py

Add prompts for your own classes after training:
    python tools/make_prompts.py --extra door stairs car

Bangla (needs internet, Microsoft neural voice):
    pip install edge-tts numpy miniaudio
    python tools/make_prompts.py --engine edge --voice bn-BD-NabanitaNeural --bangla
"""
import argparse
import asyncio
import pathlib
import tempfile
import wave

import numpy as np

OUT_RATE = 16000

# file name -> English text, Bangla text
PROMPTS = {
    "ready":        ("Vision assist ready", "ভিশন অ্যাসিস্ট প্রস্তুত"),
    "nothing":      ("Nothing ahead", "সামনে কিছু নেই"),
    "person":       ("Person", "মানুষ"),
    "left":         ("on your left", "বাঁ দিকে"),
    "ahead":        ("ahead", "সামনে"),
    "right":        ("on your right", "ডান দিকে"),
    "close":        ("very close", "খুব কাছে"),
    "one_metre":    ("one metre", "এক মিটার"),
    "two_metres":   ("two metres", "দুই মিটার"),
    "far":          ("far", "দূরে"),
    "photo_saved":  ("Photo saved", "ছবি সংরক্ষিত"),
    "photo_failed": ("Photo failed", "ছবি তোলা যায়নি"),
    "no_card":      ("No memory card", "মেমোরি কার্ড নেই"),
    "sound_on":     ("Sound on", "শব্দ চালু"),
    "battery_low":  ("Battery low", "ব্যাটারি কম"),
}


def to_16k_mono(samples, rate, channels):
    x = samples.astype(np.float32)
    if channels > 1:
        x = x.reshape(-1, channels).mean(axis=1)
    if rate != OUT_RATE:
        t_old = np.arange(len(x)) / rate
        t_new = np.arange(int(len(x) * OUT_RATE / rate)) / OUT_RATE
        x = np.interp(t_new, t_old, x)
    # trim silence at both ends and normalise to -3 dBFS
    nz = np.flatnonzero(np.abs(x) > 300)
    if len(nz):
        x = x[max(nz[0] - 800, 0):nz[-1] + 1600]
    peak = np.max(np.abs(x)) or 1.0
    return (x / peak * 23000).astype(np.int16)


def write_wav(path, pcm):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(OUT_RATE)
        w.writeframes(pcm.tobytes())


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        data = np.frombuffer(w.readframes(w.getnframes()), np.int16)
        return data, w.getframerate(), w.getnchannels()


def make_pyttsx3(items, out, rate):
    import pyttsx3
    eng = pyttsx3.init()
    eng.setProperty("rate", rate)
    tmp = pathlib.Path(tempfile.mkdtemp())
    for name, text in items:
        eng.save_to_file(text, str(tmp / f"{name}.wav"))
    eng.runAndWait()
    for name, _ in items:
        write_wav(out / f"{name}.wav", to_16k_mono(*read_wav(tmp / f"{name}.wav")))


def make_edge(items, out, voice):
    import edge_tts
    import miniaudio
    async def run():
        for name, text in items:
            mp3 = out / f"{name}.mp3"
            await edge_tts.Communicate(text, voice).save(str(mp3))
            d = miniaudio.decode_file(str(mp3), output_format=miniaudio.SampleFormat.SIGNED16)
            pcm = np.frombuffer(d.samples, np.int16)
            write_wav(out / f"{name}.wav", to_16k_mono(pcm, d.sample_rate, d.nchannels))
            mp3.unlink()
    asyncio.run(run())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parents[1] / "sdcard/prompts"))
    ap.add_argument("--engine", choices=["pyttsx3", "edge"], default="pyttsx3")
    ap.add_argument("--voice", default="en-GB-SoniaNeural", help="edge-tts voice name")
    ap.add_argument("--bangla", action="store_true", help="use the Bangla texts")
    ap.add_argument("--rate", type=int, default=170, help="speaking rate for pyttsx3")
    ap.add_argument("--extra", nargs="*", default=[], help="extra class names to speak as-is")
    a = ap.parse_args()

    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    items = [(k, v[1] if a.bangla else v[0]) for k, v in PROMPTS.items()]
    items += [(e, e.replace("_", " ")) for e in a.extra]
    if a.engine == "edge":
        make_edge(items, out, a.voice)
    else:
        make_pyttsx3(items, out, a.rate)
    print(f"wrote {len(items)} prompts to {out} -- copy this folder to the SD card as /prompts")


if __name__ == "__main__":
    main()
