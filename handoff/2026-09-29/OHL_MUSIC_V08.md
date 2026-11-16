# OHL Music v0.8 checkpoint — audible middle-ground ambience

## Hardware feedback that killed v0.7 as a final tuning

Edi's listening verdict on v0.7:

> "now its sending like NOTHING to the surrounds. oi vey"

Treat this as authoritative hardware feedback.

v0.7 successfully prevented the v0.6 "rear mains" failure, but overcorrected. Ordinary mastered
music could pass through three attenuation mechanisms in series:

1. sample-wise shared L/R removal,
2. an aggressively shaped diffuse gate,
3. Front Lock on the remaining unique residual.

The result was technically safe but often perceptually absent.

## v0.8 design target

**Keep the hard no-rear-mains safety rail, but make ordinary stereo width audible again.**

The rear source now has two layers.

### A. Continuous unique stereo bed

Same-polarity shared L/R content is removed. The remaining unique left/right residual is kept as a
quiet continuous widening bed.

Front Lock acts on this bed so widened/doubled vocal residue remains strongly front-biased.

### B. Diffuse side-spread

Balanced side information (L-R) is added as a controlled +/- ambience contribution.

Important safeguards:

- the spread is weighted by packet L/R balance, so a hard-panned direct source does not get mirrored
  aggressively into the opposite surround,
- adaptive ambience still decides how far this spread opens,
- raw L or R is never copied directly into a rear channel,
- the hard rear RMS budget remains the final ceiling.

## Diffuse law fixes

v0.7 used a squared diffuse gate. This starved moderate stereo content.

v0.8 changes the detector law to:

```
diffuse_open = sqrt(normalized_gate)
```

During development, the diffuse spread still accidentally multiplied by `diffuse_open` a second
time even though `surroundAmount` already contained it. That intermediate version produced only
about 1.7% rear/front RMS on the moderate-stereo fixture.

Removing the accidental second gate raised the same fixture to about 3.8%, proving the path was
correct but still slightly too quiet.

The final v0.8 checkpoint raises only the diffuse spread coefficient from 0.55 to 0.75. The
moderate-stereo regression test now requires >4% rear/front RMS and passes.

## Default tuning

Stock v0.8 defaults:

```ini
music_diffuse_threshold=0.10
music_rear_budget=0.22
```

The 0.22 rear budget is an absolute average RMS ceiling relative to the front pair. It prevents any
detector/tuning combination from turning SL/SR into a second pair of mains.

Spatial Lab presets are also retuned:

- Natural: threshold 0.14, budget 0.18
- Wide: threshold 0.08, budget 0.26
- Ambient: threshold 0.06, budget 0.28
- Base: threshold 0.10, budget 0.22

The Spatial Lab title identifies this generation as v0.8 dual-layer ambience.

## Upgrade behavior

Existing configs are preserved.

The installer migrates ONLY exact untouched v0.7 stock values:

- `music_diffuse_threshold=0.18` -> `0.10`
- `music_rear_budget=0.16` -> `0.22`

If the user manually changed either setting, it is left unchanged.

## Regression invariants

The final v0.8 test suite covers:

- centered mono does not synthesize rear content,
- FL/FR stay intact and LFE stays silent,
- ordinary stereo detail produces an audible rear bed,
- diffuse content opens the rears,
- hard-panned sustained instruments remain subtle,
- widened vocal residue is suppressed by Front Lock,
- decorrelated ambience remains available,
- direct attacks are ducked rather than causing rear-channel holes,
- left/right rear asymmetry remains possible,
- rear HP/LP and trims work,
- speaker-distance timing remains correct,
- an extreme "rear mains" torture signal is still held to its configured RMS budget.

## CI / artifact

Hardware-testable executable checkpoint:

`129694d436a5522f2eac76ed719789421761ed26`

CI run #147: success.

Artifact:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `11076805430`
- SHA-256: `f2fae0e62e35c5dec4ead7fae1a3d1fea089eb0f8eb0a97a1fe0b7a69724fe24`

The downloaded artifact was independently hashed and matched GitHub exactly.

## Validation state

v0.8 is CI-validated and packaged.

Hardware/listening validation is pending.

Primary listening question:

**Does normal music now produce an obvious-but-small widening/ambience contribution from SL/SR,
without individual band members sounding duplicated behind the listener?**

Do not call v0.8 hardware-validated until Edi listens to run #147.
