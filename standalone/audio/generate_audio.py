#!/usr/bin/env python3
"""Generate the original Siltwalk PCM assets with no external dependencies."""
from array import array
from io import BytesIO
from math import exp, sin, tau
from pathlib import Path
from random import Random
import sys
import wave
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

RATE = 22_050
HERE = Path(__file__).resolve().parent


def pcm_wav(samples):
    pcm = array("h", (max(-32768, min(32767, round(sample))) for sample in samples))
    if sys.byteorder != "little":
        pcm.byteswap()
    output = BytesIO()
    with wave.open(output, "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(RATE)
        stream.writeframes(pcm.tobytes())
    return output.getvalue()


def normalize_peak(signal, target):
    peak = max(map(abs, signal))
    if not peak:
        raise ValueError("a sound cannot be silent")
    return [sample * target / peak for sample in signal]


def effect(group):
    seconds, seed, volume = {
        "grass": (0.141, 114188, 10600),
        "gravel": (0.133, 414210, 11800),
        "wood": (0.157, 603410, 13200),
        "stone": (0.123, 720493, 12700),
    }[group]
    rng = Random(seed)
    body = mid = 0.0
    signal = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        noise = rng.uniform(-1, 1)
        body += 0.065 * (noise - body)
        mid += 0.34 * (noise - mid)
        grit = mid - body
        air = noise - mid

        if group == "grass":
            flutter = 0.54 + 0.46 * sin(tau * 27 * t + 0.3) ** 2
            raw = (
                (0.68 * grit + 0.26 * air) * flutter * exp(-21 * t)
                + 0.37 * body * exp(-50 * t)
            )
        elif group == "gravel":
            pieces = sum(exp(-190 * (t - at)) for at in (0.013, 0.027, 0.043, 0.061, 0.078) if t >= at)
            raw = (
                0.62 * body * exp(-47 * t)
                + 0.48 * grit * exp(-30 * t)
                + (0.40 * air + 0.34 * sin(tau * 1730 * t)) * pieces
            )
        elif group == "wood":
            raw = (
                0.78 * sin(tau * (172 * t - 115 * t * t)) * exp(-34 * t)
                + 0.29 * sin(tau * 381 * t + 0.6) * exp(-41 * t)
                + 0.38 * grit * exp(-91 * t)
            )
        else:
            raw = (
                0.62 * sin(tau * 549 * t) * exp(-53 * t)
                + 0.27 * sin(tau * 1433 * t) * exp(-91 * t)
                + 0.22 * sin(tau * 2339 * t) * exp(-115 * t)
                + 0.31 * air * exp(-132 * t)
            )
        attack = min(t / 0.003, 1.0)
        fade = min((seconds - t) / 0.019, 1.0)
        signal.append(raw * attack * fade)
    return pcm_wav(normalize_peak(signal, volume))


BEAT = 0.625
BARS = (
    # Bass, chord voicing, and the six staggered lead plucks.
    (38, (53, 57, 60), (69, 67, 65, 69, 72, 67)),
    (43, (53, 57, 59), (71, 74, 69, 67, 71, 65)),
    (36, (55, 60, 62), (64, 72, 67, 74, 72, 69)),
    (45, (55, 57, 60), (67, 64, 69, 72, 65, 64)),
)


def add_note(signal, start, length, pitch, weight, voice):
    offset = round(start * RATE)
    count = round(length * RATE)
    frequency = 440 * 2 ** ((pitch - 69) / 12)
    if voice == "pluck":
        attack, fall = 0.011, 5.9
    elif voice == "bass":
        attack, fall = 0.052, 1.7
    else:
        attack, fall = 0.18, 0.77
    for i in range(count):
        t = i / RATE
        ph = tau * frequency * t
        fade = min((length - t) / 0.105, 1)
        envelope = min(t / attack, 1) * exp(-fall * t) * fade
        if voice == "pluck":
            waveform = sin(ph) + 0.25 * sin(ph * 2.003) * exp(-11 * t)
            waveform += 0.15 * sin(ph * 3.014) * exp(-18 * t)
        elif voice == "bass":
            waveform = sin(ph) + 0.18 * sin(ph * 2)
        else:
            waveform = 0.76 * sin(ph) + 0.24 * sin(ph * 1.002)
        signal[(offset + i) % len(signal)] += waveform * envelope * weight


def bgm():
    # A ten-second cyclic mix based on an original Dorian four-bar motif.
    signal = [0.0] * round(len(BARS) * 4 * BEAT * RATE)
    for index, (bass, chord, motif) in enumerate(BARS):
        bar = index * 4 * BEAT
        add_note(signal, bar, 1.17, bass, 0.62, "bass")
        add_note(signal, bar + 2.45 * BEAT, 0.91, bass + 12, 0.25, "bass")
        for note_index, note in enumerate(chord):
            add_note(signal, bar + 0.035 * note_index, 2.0, note, 0.135, "pad")
        for beat, note in zip((0.27, 0.81, 1.58, 2.17, 2.73, 3.51), motif):
            at = bar + beat * BEAT
            add_note(signal, at, 0.69, note, 0.54, "pluck")
            add_note(signal, at + 0.335, 0.42, note, 0.096, "pluck")
    return pcm_wav(normalize_peak(signal, 7000))


def main():
    generated = {k: effect(k) for k in ("grass", "gravel", "wood", "stone")}
    with ZipFile(HERE / "classicube.zip", "w", compression=ZIP_DEFLATED, compresslevel=6) as pack:
        # The backend reuses dig PCM for matching step aliases. Keep dig entries first.
        for verb in ("dig", "step"):
            for group, wav in generated.items():
                entry = ZipInfo(f"{verb}_{group}1.wav")
                entry.compress_type = ZIP_DEFLATED
                pack.writestr(entry, wav, compresslevel=6)
    (HERE / "bgm.wav").write_bytes(bgm())


if __name__ == "__main__":
    main()
