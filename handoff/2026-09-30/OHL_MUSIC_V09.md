# OHL Music v0.9 checkpoint — linear M/S rear field

## Hardware feedback that killed the v0.8 rear extractor

Edi's direct listening verdict on v0.8:

> "the baks are really gargleded  and distorted now! and it sounds like. the same. the front stage sounds AMAZING the back is fucked"

Treat this as authoritative hardware feedback.

The front stage remained excellent because OHL Music passes FL/FR through untouched. The audible
damage was isolated to the synthesized rear path.

## Root cause

v0.7/v0.8 used a sample-wise helper equivalent to:

```cpp
if (L and R have the same polarity)
    shared = sign(L) * min(abs(L), abs(R));
else
    shared = 0;

rearL_source = L - shared;
rearR_source = R - shared;
```

This is nonlinear waveform surgery.

The subtraction coefficient changes every sample as L and R cross, change magnitude, or change
polarity. On dense real music this can create rough/granular/gargled rear waveforms even when packet
RMS, correlation and steering metrics look reasonable.

**This technique is rejected permanently. Do not reintroduce sample-wise min-magnitude/common-content
carving in the rear audio path.**

## v0.9 architecture

The rear audio path is now linear.

### Source

```
M = (L + R) / 2
S = (L - R) / 2
```

Only `S` is used for synthesized SL/SR.

Consequences:

- exact mono / exact center -> S = 0 -> no rear synthesis,
- centered vocals do not require sample-wise subtraction,
- stereo difference information remains available,
- the rear waveform is not clipped, carved or remapped sample by sample.

### Decorrelation

SL and SR receive S through two different cascaded first-order all-pass networks.

Left coefficients:

- +0.43
- -0.61

Right coefficients:

- -0.37
- +0.69

These filters are stable and flat-magnitude. They alter phase only, so they can reduce SL/SR
coherence without creating amplitude modulation or synthetic reverb gain.

### Steering / gain controls

Packet-level analysis still controls:

- adaptive ambience amount,
- Front Lock,
- direct-event ducking,
- directional asymmetry,
- hard rear RMS budget.

These operate as gains only.

No steering decision changes the rear waveform shape sample-by-sample.

### Directionality

Balanced stereo gives both decorrelation networks full directional weight.

As one input channel becomes weak, only the opposite surround is attenuated. A fully hard-left
fixture gives approximately:

- SL directional weight = 1.0
- SR directional weight = 0.1

This preserves asymmetry without turning balanced ambience down.

## Regression test for nonlinear distortion

v0.9 adds a fixed-path superposition test.

With adaptive ambience, Front Lock, transient ducking and the rear budget disabled/fixed, it verifies:

```
rear(A + B) == rear(A) + rear(B)
```

for two anti-phase sine components.

This directly guards the property that the rear audio path is linear. The previous sample-wise
common subtraction could not satisfy this property.

## CI / artifact

Hardware-testable executable checkpoint:

`ea58a6ad49883defd708e3e2fc1b2133303b5a1f`

CI run #154: success.

Artifact:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `11135569909`
- SHA-256: `66019cba64f77956fc9c9c4e57cbad65f3b2c3dd9e02695196e4ebdfcb31d0c0`

The downloaded artifact was independently hashed and matched GitHub exactly.

## Validation status

v0.9 is CI-validated.

Hardware/listening validation is pending.

Primary listening question:

**Are the rear channels now clean and smooth rather than gargled/granular, while retaining the
excellent front stage?**

Secondary questions:

- Is the rear level appropriate?
- Is vocal presence behind the listener acceptable?
- Does the all-pass decorrelation sound spacious rather than phasey?
- Does hard-panned programme remain mostly on its corresponding side?

Do not tune rear level aggressively until the first question is answered. Clean waveform quality is
the gating requirement for any further spatial tuning.
