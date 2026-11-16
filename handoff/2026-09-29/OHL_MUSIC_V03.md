# OHL Music v0.3 checkpoint — 2026-09-29

## Source of truth

Repository: `Edi-Gaming/auto-layout-virtual-ac3-encoder`

Branch: `feature/auto-layout`

PR: #1 — keep open; do not merge unless Edi explicitly asks.

Hardware-testable v0.3 code checkpoint:

`3025c79aab2e22198eec77ca259ac5996eb94043`

CI run #84 passed Windows build, unit tests, artifact assembly, and upload.

Artifact:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `11068230955`
- digest: `sha256:bfa45df2b0c3b5c9c1e7a0bf48a53dfca7fe8e220182b9dfeb70193c82e1f2ec`

Documentation commits may follow the executable checkpoint. Always fresh-fetch branch HEAD before
editing.

## v0.2 hardware listening feedback

Edi reported:

- spatiality was better than v0.1,
- but even after tuning it sounded too reverberant / cave-like,
- the rear channels contained too much apparent main-program energy,
- claps could localize clearly behind the listening position,
- that effect was not always unpleasant, but the target is more ambience behind the listener and
  fewer main musical events behind the listener.

This feedback identified the v0.2 always-on same-side width bed as the wrong abstraction. It copied
some common/direct programme into the rears by design, which created width but also made the rear
speakers sound like additional programme speakers.

## v0.3 DSP topology

Files:

- `engine/src/OhlMusicUpmixer.h`
- `engine/src/OhlMusicUpmixer.cpp`

Core rule:

**No common/direct L/R programme copy is used to manufacture rear width.**

Rear energy now starts from stereo side/difference residue:

`side = 0.5 * (L - R)`

A three-band analysis estimates ambience independently in:

- low: <~300 Hz
- mid: ~300-3000 Hz
- high: >~3000 Hz

Each band uses:

- L/R correlation
- L/R balance
- mid-vs-side energy

The combined ambience score weights mids/highs much more heavily than bass.

The base width control now scales side/difference material only. Centered mono/common information
therefore produces essentially zero rear output regardless of width setting.

The v0.2 rear all-pass coloration was removed.

## Direct-event rejection

New config key:

```ini
music_direct_reject=0.78
```

The DSP has two sample-rate envelope followers:

- fast: follows the onset
- slow: estimates surrounding programme level

When the fast envelope jumps sufficiently above the slow envelope, rear side gain is reduced
sample-by-sample. This is intentionally local in time rather than an AC3-block gate.

Goal:

- clap/snare attack stays forward,
- ambience/reverb/stereo tail after the attack remains available behind the listener,
- sustained diffuse material recovers after the onset detector settles,
- no whole-packet collapse like v0.1.

## Reference v0.3 defaults

```ini
music_surround_gain=0.70
music_width_floor=0.16
music_direct_reject=0.78
music_center_treble_gain=0.18
music_center_treble_hz=2400
music_rear_highpass_hz=160
```

Existing installs preserve whatever values Edi already tuned. The installer only adds new missing
controls rather than overwriting existing Music parameters.

Speaker distances remain:

- FL 33 in
- C 30 in
- FR 33 in
- SL 27 in
- SR 33 in

## UI

OHL Music Settings now exposes:

- Adaptive ambience
- Base side width
- Direct-event reject
- Center sparkle
- Center HP
- Rear HP
- FL/C/FR/SL/SR distances

Apply Live behavior remains unchanged: persist managed keys, send `reload`, rebuild surround
pipeline, keep daemon alive.

## Validation state

v0.3 DSP/test checkpoint `64845762f1d7e61dece23a3f9932619d3819ecca` passed CI.

Final hardware-testable code checkpoint `3025c79aab2e22198eec77ca259ac5996eb94043`
passed CI run #84 end to end.

Behavioral tests explicitly cover:

- FL/FR preservation and silent LFE
- centered mono producing no synthesized rear output
- subtle stereo-difference material still producing a low-level width field
- anti-phase/diffuse material opening strongly
- hard-panned clap onset being heavily reduced by direct-event rejection
- sustained ambience surviving after the onset detector settles
- center sparkle treble preference
- measured speaker-distance delay

OHL Music v0.3 is **CI-validated, hardware/listening validation pending**.

Do not call v0.3 hardware-validated until Edi tests it on the STR-K900 setup.

## First hardware tuning order

Recommended starting point after installing v0.3:

1. Base side width: 12-18%
2. Adaptive ambience: 55-70%
3. Direct-event reject: 75-90%
4. Center sparkle to taste
5. Rear HP to taste

Primary subjective target:

**The room should open behind the listener without the music sounding like it is being performed
from the rear speakers.**
