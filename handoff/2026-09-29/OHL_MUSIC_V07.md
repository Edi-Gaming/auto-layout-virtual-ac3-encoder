# OHL Music v0.7 checkpoint — sparse ambience

## Hardware feedback that killed v0.6

Edi's direct listening verdict on v0.6:

> "this is WORSE! IN EVERY WAY! now i hear the whole band behind me. bro the surrounds neeed to add SMALL amounts of info not triplicate evry member and shove them in my ass"

Treat this as authoritative hardware feedback.

v0.6's FIR carve solved the wrong problem. It made the rear source effectively "raw L/R outside the
carved vocal band". Even if the vocal band was attenuated correctly, this still duplicated drums,
guitars, bass harmonics, and other programme material into SL/SR.

That architecture is rejected.

## v0.7 design rule

**The rear channels start from zero and must earn every sample.**

Raw L or R is never used directly as the surround source.

The rear source is:

1. sample-wise same-polarity L/R shared content is removed,
2. only the leftover unique/asymmetric residual remains,
3. coherent-center confidence suppresses widened/doubled residuals,
4. a diffuse-content gate determines how much adaptive ambience opens,
5. transient protection ducks direct attacks but may not mute the rear bed,
6. a hard rear RMS budget limits average rear-channel energy relative to the fronts.

## New controls

```ini
music_diffuse_threshold=0.18
music_rear_budget=0.16
```

`music_diffuse_threshold`:

- 0 = adaptive ambience opens easily,
- higher = requires more balanced/decorrelated/room-like stereo evidence.

`music_rear_budget`:

- maximum average rear-channel RMS divided by average front-channel RMS,
- 0.16 means the surround pair can never become a second pair of mains even if all other controls
  are set aggressively.

The Spatial Lab exposes these directly as **Diffuse gate** and **Rear budget**.

The obsolete v0.6 FIR lock-band controls are removed from the active UI and config path.

## Transient behavior

v0.7 softens direct-event suppression.

The user-facing Direct Event Reject no longer has permission to drive the rear field to zero.
Internally its maximum effective attenuation is limited so claps/snares can duck but cannot punch
obvious holes in sustained ambience.

## CI behavioral measurements

Exact hardware-testable executable checkpoint:

`660369cec47dfe7b8f446ce36030172c088d9784`

CI run #138: success.

Artifact:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `11073488722`
- SHA-256: `f53fa9a80aa04ad2b16ed2220e9d1436558b49f937f111afcd24ec8ed687e7b0`

Representative test measurements:

- maximally diffuse stereo fixture:
  - front RMS ~0.21213
  - rear RMS ~0.03395
  - rear/front ~0.160, exactly constrained by the 0.16 budget

- hard-panned sustained instrument:
  - front RMS ~0.24748
  - rear RMS ~0.00971
  - rear/front ~0.039

- widened vocal fixture:
  - Front Lock 0: rear RMS ~0.00304
  - Front Lock 0.95: rear RMS ~0.000314
  - roughly 90% suppression of the leftover widened vocal residual

- 9 kHz decorrelated ambience:
  - Front Lock 0 and 1 remain effectively identical when rear budget is disabled for the isolated
    behavior test

- rear-main torture fixture with extreme surround settings and rear budget = 0.10:
  - front RMS ~0.24760
  - rear RMS ~0.02474
  - hard ceiling holds at ~10%

## Validation status

v0.7 is CI-validated and packaged.

Hardware/listening validation is still pending.

Primary listening question:

**Do SL/SR now behave as a subtle spatial contribution rather than audibly duplicating each band
member?**

If v0.7 still sounds too populated behind the listener, adjust the extraction/gating itself before
raising front-lock complexity. Do not return to any topology whose rear source contains raw L/R
programme outside a carve band.
