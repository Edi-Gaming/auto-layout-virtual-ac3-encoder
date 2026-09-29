# OHL Music v0.1 checkpoint — 2026-09-29

## Source of truth

Repository: `Edi-Gaming/auto-layout-virtual-ac3-encoder`

Branch: `feature/auto-layout`

PR: #1 — keep open; do not merge unless Edi explicitly asks.

The code checkpoint immediately before this note is:

`c5aad87f4a129f4045a4c3f5a28a04683ebebdaa` — **Document experimental OHL Music v0.1**

Always fresh-fetch the branch HEAD before editing.

## Motivation

The Sony STR-K900's PLII Music processing is enjoyable, but Edi's physical center speaker is a
small Bose Acoustimass cube with an audible timbre/sibilance mismatch versus the main fronts.
OHL can already emit discrete AC3 5.1, so stereo music no longer has to depend on the receiver's
matrix decoder. The first OHL Music profile is therefore phantom-center-first.

## OHL Music v0.1 architecture

The feature is deliberately opt-in:

```ini
stereo_processing=receiver   # safe/default existing behavior
# stereo_processing=music    # experimental OHL Music
```

With `receiver`, existing behavior is preserved: stereo -> genuine AC3 2.0 -> receiver PLII/A.F.D.

With `music`, stereo/quasi-stereo input follows:

`FL/FR PCM -> OhlMusicUpmixer -> discrete float 5.1 -> dedicated AC3 5.1 encoder -> IEC61937`

v0.1 policy:

- FL = original L.
- FR = original R.
- C = silent (phantom center).
- LFE = silent (no synthesized bass management).
- SL/SR = correlation-aware stereo-difference ambience at a conservative/moderately enveloping level.
- Balanced anti-phase/decorrelated information opens into the surrounds.
- Hard-panned direct sources are strongly suppressed in the derived surround bed.
- High-crest/transient-heavy blocks receive additional rear attenuation.
- SL/SR use opposite polarity to avoid a stable rear phantom-center image.
- Speaker-distance delays are applied to the generated six-channel PCM before AC3 encoding.

Native multichannel always wins. Actual source activity in C/LFE/SL/SR above the existing
auto-layout threshold bypasses OHL Music and uses the pre-existing native `enc51_` path.
The existing hold timer prevents rapid flapping back from native 5.1.

OHL Music has its own `encMusic51_` so the hardware-validated native 5.1 encoder path is not
repurposed or reconfigured.

## Initial tuning / room geometry

Edi's listening-position measurements on 2026-09-29:

- FL: 33 in
- C: 30 in
- FR: 33 in
- SL: 27 in
- SR: 33 in

At 48 kHz with 343 m/s nominal speed of sound, integer-sample alignment currently resolves to:

- FL: +0 samples
- FR: +0 samples
- C: +11 samples
- SL: +21 samples
- SR: +0 samples

C is currently silent but its delay is already represented for future center-width modes.

Initial surround strength:

`music_surround_gain=0.55`

Intent: slightly more enveloping than Edi's current PLII Music reference, but not aggressive.

## Validation state

DSP-core CI checkpoint:

`5c76e6853ca3e4b2ea1494d7635d128f02425bf4`

CI run #53:
- Windows build: success
- unit tests: success
- behavioral tests cover phantom-center preservation, anti-phase ambience extraction,
  hard-pan suppression, and measured delay geometry.

Live integration checkpoint:

`ecee7c28ebda112f2a238e18117529a319d90a99`

CI run #56:
- Windows build: success
- unit tests: success
- artifact assembly: success

OHL Music has **not yet been hardware/listening validated** at the time of this note.

The previous hidden-startup build was installed on Edi's PC and Edi reported that it works.
Do not infer additional reboot/recovery-shortcut validation details beyond what Edi explicitly
confirms later.

## First listening references

Edi selected:

- `Du riechst so gut`
- `THAT GUY` — Tyler, the Creator

Listen first for:

- lead vocal staying solidly phantom-center with no Bose center contribution,
- kick/snare/front attacks remaining anchored forward,
- reverbs/doubles/phasey stereo production spreading into SL/SR,
- hard-panned instruments not jumping distractingly behind the listener,
- SL timing no longer perceptually arriving early despite being physically closer,
- overall rear level being just above the current PLII Music preference without becoming obvious.

## Next steps after listening

Do not jump straight to frequency-domain steering. First tune v0.1 from hardware observations:
surround gain, correlation weighting, hard-pan suppression, transient weighting, and delays.

Once the conservative version earns its keep, candidates for v0.2 include multiband coherence,
frequency-dependent steering, proper all-pass decorrelation, center-width control, trims, richer
live diagnostics, and tray/profile switching.
