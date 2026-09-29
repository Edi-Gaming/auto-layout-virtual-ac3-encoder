# OHL Music v0.5 checkpoint — 2026-09-29

## Source of truth

Repository: `Edi-Gaming/auto-layout-virtual-ac3-encoder`

Branch: `feature/auto-layout`

PR: #1 — keep open; do not merge unless Edi explicitly asks.

Hardware-testable v0.5 code checkpoint:

`9b0ff48ffcdf68e1a3a7d25cce0968a3c9f10eab`

CI run #109 passed Windows build, full unit/DSP suite, packaging, and artifact upload.

Artifact:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `11068444600`
- digest: `sha256:5c25381ba308f09bbf9686fdfbe9442fe525a03ded81a7ecc2cb18972b4826c9`

## Hardware feedback that triggered v0.5

Edi's v0.4 listening feedback:

- too much voice was audible from behind,
- lead voice should remain mostly in front,
- vocal reverb trails behind are desirable,
- rear image had become too symmetrical,
- presentation resembled a hall effect layered on top of stereo.

This exposed two separate problems:

1. v0.4 transient rejection could suppress attacks, but sustained centered vocals were not
   transients and could continue feeding stereo residue to the rears.
2. v0.4 still generated SL/SR from a mirrored +/- side pair, which produced a spatially balanced,
   synthetic-hall character even when the original recording was not that symmetric.

## v0.5 DSP topology

### 1. Coherent center subtraction

Rear source is no longer simply:

```
SL <-  L-R
SR <- -(L-R)
```

v0.5 first estimates coherent/centered content from full-band and especially midband
correlation/balance, then subtracts that coherent center from L and R independently.

Conceptually:

```
center_estimate = mid * center_confidence
rear_source_L   = L - center_estimate
rear_source_R   = R - center_estimate
```

Coherent-center subtraction is structural and always active. Even if the user turns Front Lock to
0, centered mono content must still not be manufactured into the rear channels.

### 2. Front / vocal lock

New config key:

```ini
music_front_lock=0.88
```

The classifier is dominated by ~300-3000 Hz coherence because that is where lead-vocal/instrument
body usually anchors.

High confidence:
- coherent,
- balanced L/R,
- midrange-dominant,
- likely front-anchored vocal/instrument body.

Low confidence:
- decorrelated,
- unbalanced,
- diffuse stereo tail/room information.

`music_front_lock` applies additional attenuation to the rear residual when confidence is high.

Goal:
- sustained vocal body remains forward,
- stereo/decorrelated vocal reverb tails remain available in SL/SR,
- this is independent of transient/direct-event rejection.

### 3. Independent asymmetric rear channels

After center subtraction, SL and SR use the independent L and R residuals.

This deliberately stops forcing equal/opposite rear signals.

A left-heavy reflection or spatial effect can remain left-heavy in the rear field. A symmetric
rear field now occurs only when the source residual itself is symmetric.

This is intended to remove the "hall effect layered on top of stereo" character.

## Front Lock UI

OHL Music Spatial Lab now exposes:

**Front / vocal lock**

in the **Front anchoring / direct events** group.

This is separate from:

- Direct-event reject
- transient sensitivity ratio
- transient recovery

because sustained vocal anchoring and onset/percussion anchoring are different problems.

Preset defaults now include Front Lock:

- Natural: 0.94
- Wide: 0.86
- Ambient: 0.96
- baseline: 0.88

## Validation

Run #109 passed the complete CI suite.

New tests demonstrate:

### Vocal-like correlated residue

Synthetic centered midrange voice + small stereo residue:

- Front Lock 0: rear RMS ~0.00943
- Front Lock 0.95: rear RMS ~0.00098

Approximately 90% reduction in the rear vocal-like residue.

### Diffuse ambience preservation

Anti-phase 5.2 kHz diffuse field:

- Front Lock 0: rear RMS ~0.16777
- Front Lock 1: rear RMS ~0.16777

Front Lock does not attenuate this decorrelated ambience case.

### Rear asymmetry

Left-only spatial residue:

- SL RMS ~0.05206
- SR RMS ~0

The new topology no longer mirrors the same spatial cue into both surrounds.

Existing invariants continue to pass after isolating their tests from Front Lock:

- centered mono does not synthesize rear content,
- base side width still passes genuine stereo difference,
- direct-event rejection suppresses clap onset,
- sustained ambience survives,
- band-weight controls work,
- rear HP/LP and trims work,
- center sparkle band remains bounded,
- FL/FR preservation, silent LFE, and speaker-distance timing remain intact.

## Hardware status

OHL Music v0.5 is **CI-validated, hardware/listening validation pending**.

Do not call v0.5 hardware-validated until Edi listens to run #109 on the STR-K900 setup.

## First listening target

Start with:

```ini
music_front_lock=0.88
```

If lead vocal is still perceptibly behind:
- raise Front Lock toward 0.95-1.00 before reducing the overall rear field.

If vocal becomes too dry / its stereo tail disappears:
- lower Front Lock slightly before changing direct-event rejection.

Primary subjective target:

**Lead vocal/body stays anchored in front; only its room/reverb/spatial tail blooms behind. Rear
space should inherit the recording's asymmetry rather than sounding like a symmetric hall preset.**
