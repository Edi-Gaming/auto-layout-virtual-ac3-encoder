# OHL Music v0.6 checkpoint — 2026-09-29

## Source of truth

Repository: `Edi-Gaming/auto-layout-virtual-ac3-encoder`

Branch: `feature/auto-layout`

PR: #1 — keep open; do not merge unless Edi explicitly asks.

Hardware-testable v0.6 executable checkpoint:

`994046b42f019ddd0f39c361162b62db329c7442`

CI run #127 passed Windows build, full unit/DSP suite, packaging, and artifact upload.

Artifact:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `11070983074`
- digest: `sha256:fd6eae0ee4b94b88a688c5624389cd3c7a258c3d20ac1824351a619f10741b29`

## Hardware feedback that triggered v0.6

Edi's verdict on v0.5:

> "i didtn really nortice a diffference. still a lot of vocal behind me"

v0.5's correlation/balance classifier was therefore not strong enough as the main mechanism for
vocal anchoring. Modern lead vocals can contain doubles, widening, stereo effects, and other
decorrelated energy even while perceptually remaining one front-anchored lead.

The v0.6 design target is more explicit:

**Do not merely classify voice as front. Physically remove a chosen vocal/body frequency band from
the rear residual, while keeping outside-band room/air and asymmetric effects available.**

## Final v0.6 DSP architecture

### Linear-phase vocal/body carve

Config:

```ini
music_front_lock=0.88
music_front_lock_low_hz=250
music_front_lock_high_hz=5200
```

The rear vocal/body extractor is a 65-tap Hamming-windowed linear-phase FIR bandpass.

At 48 kHz:

- FIR group delay: 32 samples
- fixed processing latency: ~0.667 ms

Raw rear L/R are delayed by the same 32 samples before FIR subtraction, so the vocal band and raw
signal are phase aligned.

Per-channel concept:

```
raw stereo
   |
   +--> 32-sample alignment delay ---------------------+
   |                                                   |
   +--> 65-tap FIR 250-5200 Hz --> vocal/body band ---+
                                                       |
outside band = aligned raw - vocal band <--------------+
```

For L and R independently:

1. extract the FIR vocal/body band,
2. find same-polarity shared vocal/body content,
3. remove that shared vocal component from the rear source,
4. apply Front Lock to the remaining independent vocal-band residual,
5. recombine with the outside-band content,
6. apply existing rear HP/LP, adaptive ambience gain, transient rejection, trims, and speaker
   geometry.

At Front Lock = 100%, the rear residual contains the phase-aligned outside-band content but no
remaining independent 250-5200 Hz vocal/body residual.

### Mono safety

A block that is effectively true mono (correlation > 0.9995 and near-perfect L/R balance) is forced
to zero rear source. This preserves the longstanding invariant that centered mono program cannot be
manufactured into surround content.

### Asymmetry

SL/SR remain independent residual channels, not a synthetic +/- side pair.

This preserves actual recording asymmetry and avoids returning to the v0.4 hall-like mirrored rear
field.

## Fixed FIR latency and speaker alignment

The FIR adds 32 samples to the rear extraction path.

FL/FR/FC/LFE delay lines receive the same 32-sample compensation. The user-entered distance delays
remain relative geometry on top of that common latency.

At Edi's measurements:

- FL 33"
- C 30"
- FR 33"
- SL 27"
- SR 33"

geometry-only delay reporting remains:

- FL 0
- FR 0
- C 11
- SL 21
- SR 0

The actual Music pipeline has an additional common 32-sample fixed processing latency.

## Why the earlier v0.6 experiments were rejected

Several intermediate implementations intentionally failed CI behavioral tests and were not packaged.

### Full-band common-min + one-pole IIR split

Rejected because unrelated high-frequency texture could influence full-band common subtraction and
the one-pole crossover was far too shallow.

### Steeper cascaded IIR vocal band

Rejected because the filtered vocal band had phase rotation relative to the raw signal. Subtracting
it from raw L/R did not produce clean cancellation.

### Complementary-looking IIR reconstruction

Also rejected. Without matched phase, `raw - filtered band` still retained substantial energy in
the nominal vocal range.

### FIR solution

Linear phase provides a known constant group delay. Delaying raw L/R by that exact amount makes
band subtraction physically meaningful.

## CI behavioral results

The final v0.6 exact tree passed the complete suite in run #127.

Representative measurements from the final FIR development tests:

### Widened vocal-like residual

Synthetic centered 1 kHz vocal body plus a small stereo 1.45 kHz residual:

- Front Lock 0: rear RMS ~0.01383
- Front Lock 0.95: rear RMS ~0.00243

About an 82% reduction in this test signal.

### High-frequency diffuse field

9 kHz anti-phase ambience:

- Front Lock 0: rear RMS ~0.16101
- Front Lock 1.0: rear RMS ~0.16125

Essentially unchanged.

### Asymmetric high-frequency cue

A left-only 8 kHz cue remains left-dominant in the rear field instead of being mirrored into SR.

Existing tests continue to cover:

- centered mono -> no rear synthesis,
- FL/FR preservation after fixed FIR latency,
- LFE silent,
- base width,
- ambience band weighting,
- ambience attack/release,
- transient/direct-event rejection,
- rear HP/LP,
- SL/SR trims,
- center sparkle band,
- measured speaker-distance alignment on top of FIR latency.

## Spatial Lab changes

The UI now identifies itself as:

**v0.6 FIR vocal carve — voice/body stays front; room/air can stay behind.**

Front anchoring exposes:

- Front / vocal lock
- Lock band low/high Hz
- Direct-event reject
- transient sensitivity ratio
- transient recovery

The old "v0.3" quick-start button is now labeled **BASE** because the processing topology is no
longer v0.3 even when using baseline tuning values.

Preset buttons populate the lock-band edges as well as Front Lock.

## Validation state

OHL Music v0.6 is **CI-validated, hardware/listening validation pending**.

Do not call the FIR vocal carve hardware-validated until Edi listens to run #127 on the STR-K900
setup.

## First hardware test

To make the difference deliberately obvious before subtle tuning:

```ini
music_front_lock=1.00
music_front_lock_low_hz=200
music_front_lock_high_hz=6000
```

If that finally removes the perceived rear vocal body, back Front Lock down toward 0.90-0.95 and/or
narrow the band until vocal reverb and spatial tails return without the lead sounding behind the
listener.

If even Front Lock 100% with a roughly 200-6000 Hz carve still leaves a clearly intelligible lead
vocal behind the listener, the remaining vocal information is likely genuinely encoded outside the
carve band and/or strongly decorrelated. At that point the next architecture should be a
time-frequency/STFT center-vocal estimator or source-separation-assisted mask, rather than another
broadband correlation heuristic.
