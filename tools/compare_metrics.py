#!/usr/bin/env python3
"""A/B metrics gate for comparing a rendered WAV/f0-trace pair against a
JS reference render.

Usage:
    python3 tools/compare_metrics.py A.wav B.wav --trace-a A.csv --trace-b B.csv [--report out.md]
    python3 tools/compare_metrics.py A.wav --trace-a A.csv [--report out.md]   # single-sided

A is the JS reference render, B is the C++ port render. In two-file mode the
script prints PASS/FAIL for four metrics (spectral flatness, octave jump
rate, loudness match, f0 agreement) and exits 0 iff all four pass. In
single-sided mode (only A given) it reports absolute metrics with no
PASS/FAIL gate and exits 0.

Metric definitions are binding milestone pass criteria; see
docs/superpowers/plans/2026-08-25-milestone0-host-harness.md, Task 4.
"""

import argparse
import csv
import sys
import wave

import numpy as np

SAMPLE_RATE = 48000
BLOCK_SIZE = 128
BLOCK_RATE = SAMPLE_RATE / BLOCK_SIZE  # 375 Hz

# --- Metric thresholds (from task-4-brief.md, verbatim) ---
FLATNESS_MAX = 5e-4
FLATNESS_RMS_GATE_DBFS = -40.0

JUMP_LOG2_THRESHOLD = 0.45
# 1.5/s (raised from an initial 1.0/s): the block-pair jump metric counts
# legitimate melodic leaps (> 5.4 semitones between consecutive blocks), so
# this is an absolute sanity bound, not a parity gate. The delta check below
# is the real A/B parity requirement and is intentionally much tighter.
JUMP_RATE_MAX = 1.5
JUMP_RATE_DELTA_MAX = 0.4

LOUDNESS_WINDOW_S = 0.5
LOUDNESS_GATE_DBFS = -40.0
LOUDNESS_MEDIAN_MAX_DB = 1.0
LOUDNESS_MAX_MAX_DB = 2.5

F0_MIN_HZ = 60.0
F0_MIN_AMP = 0.05
F0_MEDIAN_MAX_CENTS = 10.0
F0_OCTAVE_FRACTION_MAX = 0.05
F0_OCTAVE_CENTS = 600.0


def load_wav(path):
    """Load a 16-bit mono PCM WAV as float64 in [-1, 1], per project
    convention: int16 / 32768.0."""
    with wave.open(path, "rb") as w:
        n_channels = w.getnchannels()
        sample_width = w.getsampwidth()
        frame_rate = w.getframerate()
        n_frames = w.getnframes()
        raw = w.readframes(n_frames)

    if sample_width != 2:
        raise ValueError(f"{path}: expected 16-bit PCM, got sample width {sample_width}")

    data = np.frombuffer(raw, dtype=np.int16)
    if n_channels > 1:
        data = data.reshape(-1, n_channels)[:, 0]

    if frame_rate != SAMPLE_RATE:
        print(
            f"WARNING: {path} sample rate is {frame_rate}, expected {SAMPLE_RATE}",
            file=sys.stderr,
        )

    return data.astype(np.float64) / 32768.0


def load_trace(path):
    """Load a block/f0/amp trace CSV. Returns (f0, amp) arrays."""
    f0 = []
    amp = []
    with open(path, "r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            f0.append(float(row["f0"]))
            amp.append(float(row["amp"]))
    return np.array(f0), np.array(amp)


def dbfs(rms):
    """Convert a linear RMS amplitude to dBFS. -inf-safe for zero input."""
    rms = np.asarray(rms, dtype=np.float64)
    out = np.full_like(rms, -np.inf)
    nonzero = rms > 0
    out[nonzero] = 20.0 * np.log10(rms[nonzero])
    return out


# --- Metric 1: Spectral flatness ---


def spectral_flatness(audio, sample_rate=SAMPLE_RATE):
    """Per 1s Hann window: geometric mean / arithmetic mean of the power
    spectrum. Reported as the median over windows whose RMS > -40 dBFS.

    Returns (median_flatness, n_windows_used, n_windows_total).
    """
    win_len = int(round(sample_rate * 1.0))
    if len(audio) < win_len:
        win_len = len(audio)
    if win_len <= 0:
        return float("nan"), 0, 0

    window = np.hanning(win_len)
    flatness_values = []
    n_total = 0

    for start in range(0, len(audio) - win_len + 1, win_len):
        segment = audio[start : start + win_len]
        n_total += 1

        rms = np.sqrt(np.mean(segment**2))
        if dbfs(rms) <= FLATNESS_RMS_GATE_DBFS:
            continue

        windowed = segment * window
        spectrum = np.fft.rfft(windowed)
        power = np.abs(spectrum) ** 2
        # Drop the DC bin; it can be zero/near-zero for AC-coupled audio and
        # would otherwise dominate/degenerate the geometric mean.
        power = power[1:]
        power = power[power > 0]
        if power.size == 0:
            continue

        log_power = np.log(power)
        geo_mean = np.exp(np.mean(log_power))
        arith_mean = np.mean(power)
        if arith_mean <= 0:
            continue

        flatness_values.append(geo_mean / arith_mean)

    if not flatness_values:
        return float("nan"), 0, n_total

    return float(np.median(flatness_values)), len(flatness_values), n_total


# --- Metric 2: Octave jump rate ---


def octave_jump_rate(f0, duration_s):
    """Count blocks where |log2(f0[i]/f0[i-1])| > 0.45, divided by trace
    duration in seconds. Blocks where either f0 is <= 0 are skipped (no
    valid pitch to compare)."""
    if duration_s <= 0 or len(f0) < 2:
        return 0.0, 0

    prev = f0[:-1]
    cur = f0[1:]
    valid = (prev > 0) & (cur > 0)

    ratio = np.zeros_like(cur)
    ratio[valid] = np.abs(np.log2(cur[valid] / prev[valid]))

    jumps = int(np.sum(valid & (ratio > JUMP_LOG2_THRESHOLD)))
    rate = jumps / duration_s
    return rate, jumps


# --- Metric 3: Loudness flatness / match ---


def loudness_series(audio, sample_rate=SAMPLE_RATE, window_s=LOUDNESS_WINDOW_S):
    """RMS in dBFS per window_s window. Returns array of dBFS values, one
    per window (trailing partial window included)."""
    win_len = int(round(sample_rate * window_s))
    if win_len <= 0:
        return np.array([])

    n_windows = int(np.ceil(len(audio) / win_len))
    values = np.empty(n_windows)
    for i in range(n_windows):
        segment = audio[i * win_len : (i + 1) * win_len]
        if len(segment) == 0:
            values[i] = -np.inf
            continue
        rms = np.sqrt(np.mean(segment**2))
        values[i] = float(dbfs(np.array([rms]))[0])
    return values


def loudness_match(audio_a, audio_b, sample_rate=SAMPLE_RATE):
    """Median and max |dB_A - dB_B| over windows where the reference (A) is
    > -40 dBFS."""
    db_a = loudness_series(audio_a, sample_rate)
    db_b = loudness_series(audio_b, sample_rate)

    n = min(len(db_a), len(db_b))
    db_a = db_a[:n]
    db_b = db_b[:n]

    gate = db_a > LOUDNESS_GATE_DBFS
    if not np.any(gate):
        return float("nan"), float("nan"), 0

    diff = np.abs(db_a[gate] - db_b[gate])
    return float(np.median(diff)), float(np.max(diff)), int(np.sum(gate))


# --- Metric 4: f0 agreement ---


def f0_agreement(f0_a, amp_a, f0_b):
    """cents = 1200*|log2(fB/fA)| over blocks where both traces have
    f0 > 60 Hz and reference amp > 0.05. Returns (median_cents,
    octave_fraction, n_blocks_used)."""
    n = min(len(f0_a), len(f0_b), len(amp_a))
    f0_a = f0_a[:n]
    f0_b = f0_b[:n]
    amp_a = amp_a[:n]

    mask = (f0_a > F0_MIN_HZ) & (f0_b > F0_MIN_HZ) & (amp_a > F0_MIN_AMP)
    if not np.any(mask):
        return float("nan"), float("nan"), 0

    cents = 1200.0 * np.abs(np.log2(f0_b[mask] / f0_a[mask]))
    median_cents = float(np.median(cents))
    octave_fraction = float(np.mean(cents > F0_OCTAVE_CENTS))
    return median_cents, octave_fraction, int(np.sum(mask))


def fmt(value, digits=4):
    if isinstance(value, float) and (np.isnan(value) or np.isinf(value)):
        return str(value)
    return f"{value:.{digits}f}"


def run_single_sided(label, wav_path, trace_path):
    audio = load_wav(wav_path)
    # Single-sided mode only reports flatness and jump rate (no B trace to
    # compare against), so the trace's amp column is not needed here.
    f0, _amp = load_trace(trace_path)
    duration_s = len(f0) * BLOCK_SIZE / SAMPLE_RATE

    flat_med, flat_used, flat_total = spectral_flatness(audio)
    jump_rate, jump_count = octave_jump_rate(f0, duration_s)

    lines = []
    lines.append(f"# Single-sided metrics report: {label}")
    lines.append("")
    lines.append(f"WAV: `{wav_path}`")
    lines.append(f"Trace: `{trace_path}`")
    lines.append(f"Trace duration: {duration_s:.3f} s ({len(f0)} blocks)")
    lines.append("")
    lines.append("| Metric | Value |")
    lines.append("|---|---|")
    lines.append(
        f"| Spectral flatness (median, {flat_used}/{flat_total} windows above gate) | {fmt(flat_med, 6)} |"
    )
    lines.append(f"| Octave jump rate | {fmt(jump_rate, 4)} /s ({jump_count} jumps) |")

    text = "\n".join(lines)
    print(text)
    return text, 0


def run_two_sided(wav_a_path, wav_b_path, trace_a_path, trace_b_path):
    audio_a = load_wav(wav_a_path)
    audio_b = load_wav(wav_b_path)
    f0_a, amp_a = load_trace(trace_a_path)
    # f0 agreement gates on the *reference's* amp only (see f0_agreement's
    # docstring / task-4-brief.md: "reference amp > 0.05"), so B's amp
    # column is loaded but intentionally not used as a gate.
    f0_b, _amp_b = load_trace(trace_b_path)

    duration_a_s = len(f0_a) * BLOCK_SIZE / SAMPLE_RATE
    duration_b_s = len(f0_b) * BLOCK_SIZE / SAMPLE_RATE

    # --- Metric 1: spectral flatness ---
    flat_a, flat_a_used, flat_a_total = spectral_flatness(audio_a)
    flat_b, flat_b_used, flat_b_total = spectral_flatness(audio_b)
    flat_pass = (
        not np.isnan(flat_a)
        and not np.isnan(flat_b)
        and flat_a < FLATNESS_MAX
        and flat_b < FLATNESS_MAX
    )

    # --- Metric 2: octave jump rate ---
    jump_rate_a, jump_count_a = octave_jump_rate(f0_a, duration_a_s)
    jump_rate_b, jump_count_b = octave_jump_rate(f0_b, duration_b_s)
    jump_delta = abs(jump_rate_a - jump_rate_b)
    jump_pass = (
        jump_rate_a < JUMP_RATE_MAX
        and jump_rate_b < JUMP_RATE_MAX
        and jump_delta < JUMP_RATE_DELTA_MAX
    )

    # --- Metric 3: loudness match ---
    loud_median, loud_max, loud_n = loudness_match(audio_a, audio_b)
    loud_pass = (
        not np.isnan(loud_median)
        and loud_median < LOUDNESS_MEDIAN_MAX_DB
        and loud_max < LOUDNESS_MAX_MAX_DB
    )

    # --- Metric 4: f0 agreement ---
    f0_median_cents, f0_octave_frac, f0_n = f0_agreement(f0_a, amp_a, f0_b)
    f0_pass = (
        not np.isnan(f0_median_cents)
        and f0_median_cents < F0_MEDIAN_MAX_CENTS
        and f0_octave_frac < F0_OCTAVE_FRACTION_MAX
    )

    overall_pass = flat_pass and jump_pass and loud_pass and f0_pass

    lines = []
    lines.append("# A/B metrics report")
    lines.append("")
    lines.append(f"A (reference): `{wav_a_path}` / `{trace_a_path}`")
    lines.append(f"B (port):      `{wav_b_path}` / `{trace_b_path}`")
    lines.append("")
    lines.append("| Metric | Value | Threshold | Result |")
    lines.append("|---|---|---|---|")

    lines.append(
        f"| Spectral flatness A (median, {flat_a_used}/{flat_a_total} windows) | {fmt(flat_a, 6)} | < {FLATNESS_MAX:g} | "
        f"{'PASS' if (not np.isnan(flat_a) and flat_a < FLATNESS_MAX) else 'FAIL'} |"
    )
    lines.append(
        f"| Spectral flatness B (median, {flat_b_used}/{flat_b_total} windows) | {fmt(flat_b, 6)} | < {FLATNESS_MAX:g} | "
        f"{'PASS' if (not np.isnan(flat_b) and flat_b < FLATNESS_MAX) else 'FAIL'} |"
    )
    lines.append(
        f"| **Spectral flatness (overall)** | A={fmt(flat_a, 6)}, B={fmt(flat_b, 6)} | both < {FLATNESS_MAX:g} | "
        f"**{'PASS' if flat_pass else 'FAIL'}** |"
    )

    lines.append(
        f"| Octave jump rate A | {fmt(jump_rate_a, 4)}/s ({jump_count_a} jumps) | < {JUMP_RATE_MAX:g}/s | "
        f"{'PASS' if jump_rate_a < JUMP_RATE_MAX else 'FAIL'} |"
    )
    lines.append(
        f"| Octave jump rate B | {fmt(jump_rate_b, 4)}/s ({jump_count_b} jumps) | < {JUMP_RATE_MAX:g}/s | "
        f"{'PASS' if jump_rate_b < JUMP_RATE_MAX else 'FAIL'} |"
    )
    lines.append(
        f"| **Octave jump rate (overall)** | delta={fmt(jump_delta, 4)}/s | both < {JUMP_RATE_MAX:g}/s AND delta < {JUMP_RATE_DELTA_MAX:g}/s | "
        f"**{'PASS' if jump_pass else 'FAIL'}** |"
    )

    lines.append(
        f"| **Loudness match** | median={fmt(loud_median, 4)} dB, max={fmt(loud_max, 4)} dB ({loud_n} windows gated) | "
        f"median < {LOUDNESS_MEDIAN_MAX_DB:g} dB AND max < {LOUDNESS_MAX_MAX_DB:g} dB | **{'PASS' if loud_pass else 'FAIL'}** |"
    )

    lines.append(
        f"| **f0 agreement** | median={fmt(f0_median_cents, 4)} cents, octave-class frac={fmt(f0_octave_frac, 4)} ({f0_n} blocks gated) | "
        f"median < {F0_MEDIAN_MAX_CENTS:g} cents AND frac < {F0_OCTAVE_FRACTION_MAX:g} | **{'PASS' if f0_pass else 'FAIL'}** |"
    )

    lines.append("")
    lines.append(f"## Overall: {'PASS' if overall_pass else 'FAIL'}")

    text = "\n".join(lines)
    print(text)
    return text, (0 if overall_pass else 1)


def main():
    parser = argparse.ArgumentParser(
        description="A/B metrics gate comparing a C++ port render against a JS reference render."
    )
    parser.add_argument("wav_a", help="Reference (JS) WAV file")
    parser.add_argument("wav_b", nargs="?", default=None, help="Port (C++) WAV file (omit for single-sided report)")
    parser.add_argument("--trace-a", required=True, help="Reference f0 trace CSV (block,f0,amp)")
    parser.add_argument("--trace-b", default=None, help="Port f0 trace CSV (block,f0,amp)")
    parser.add_argument("--report", default=None, help="Write the report as markdown to this path")

    args = parser.parse_args()

    two_sided = args.wav_b is not None
    if two_sided and args.trace_b is None:
        parser.error("--trace-b is required when a second WAV (B) is given")
    if not two_sided and args.trace_b is not None:
        parser.error("--trace-b was given but no B WAV was given; both or neither")

    if two_sided:
        text, exit_code = run_two_sided(args.wav_a, args.wav_b, args.trace_a, args.trace_b)
    else:
        text, exit_code = run_single_sided("A", args.wav_a, args.trace_a)

    if args.report:
        with open(args.report, "w") as f:
            f.write(text + "\n")

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
