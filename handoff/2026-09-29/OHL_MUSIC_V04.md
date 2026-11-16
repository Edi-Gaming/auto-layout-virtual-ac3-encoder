# OHL Music v0.4 checkpoint — 2026-09-29

## Source of truth

Repository: `Edi-Gaming/auto-layout-virtual-ac3-encoder`

Branch: `feature/auto-layout`

PR: #1 — keep open; do not merge unless Edi explicitly asks.

Hardware-testable v0.4 code checkpoint:

`b5a550d87b1ba274fd97ab7b2bec0cfef1a5ff50`

CI run #97 passed:

- Windows configure/build
- full unit/DSP test suite
- day-to-day artifact assembly
- artifact upload

Artifact:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `11068348319`
- digest: `sha256:3fdab2b7c8225215fe91e29416b5eef6a22ca3878254c856f539b418ca8e2e68`

Documentation commits may follow the executable checkpoint. Always fresh-fetch branch HEAD before
editing.

## Hardware feedback that triggered v0.4

Edi's first hardware verdict on v0.3:

> "better. way better. but i want more tuning to happen. more features and customizability too"

Interpretation:

- v0.3's ambience-first topology is now the baseline and should not be casually replaced.
- Continue iterating through exposed parameters rather than another wholesale spatial rewrite.
- Make subjective tuning fast enough to do by ear while music is playing.
- Preserve existing tuned installs instead of silently resetting them to new defaults.

## v0.4 design rule

**Keep v0.3 routing; expose the useful constants.**

Still unchanged:

- FL/FR preserve the source stereo program.
- Rear energy originates from stereo-difference/diffuse residue only.
- Centered/mono content does not get copied behind the listener.
- Direct-event rejection is sample-local, not a whole AC3-frame gate.
- Center is a band-limited presence/sparkle feed.
- LFE remains unsynthesized.
- Native source C/LFE/surround activity bypasses OHL Music and uses the existing native 5.1 path.
- Speaker-distance time alignment stays before AC3 encoding.

## New v0.4 DSP controls

### Spatial / ambience

```ini
music_surround_gain=0.70
music_width_floor=0.16

music_ambience_low_weight=0.08
music_ambience_mid_weight=0.46
music_ambience_high_weight=0.46
music_ambience_attack_ms=100
music_ambience_release_ms=520
```

Meaning:

- `music_surround_gain`: maximum adaptive diffuse contribution.
- `music_width_floor`: base side/difference feed only; still no common-program rear copy.
- low/mid/high weights: contribution of <~300 Hz, ~300–3000 Hz, and >~3000 Hz diffuseness to
  the adaptive ambience score.
- attack/release: block-level steering time constants. Defaults approximate the successful v0.3
  smoothing.

### Direct-event control

```ini
music_direct_reject=0.78
music_direct_threshold=1.45
music_direct_recovery_ms=18
```

- Reject: maximum onset attenuation in surrounds.
- Threshold: fast/slow envelope ratio where onset classification starts. Lower catches more events.
- Recovery: fast detector release; lower values let ambience return sooner after a clap/snare.

### Center band

```ini
music_center_treble_gain=0.18
music_center_treble_hz=2400
music_center_lowpass_hz=16000
```

The physical center now has an explicit HP/LP band, rather than only a high-pass. This allows Edi
to use the bright Bose cube as a controlled presence band without unnecessary upper-treble/sibilant
energy.

### Rear voicing / balance

```ini
music_rear_highpass_hz=160
music_rear_lowpass_hz=18000
music_rear_left_trim=1.00
music_rear_right_trim=1.00
```

- Rear HP/LP create an explicit ambience band.
- Independent SL/SR trims compensate room/speaker asymmetry independently of distance timing.

## OHL Music Spatial Lab UI

`engine.exe --music-settings` is now titled **OHL Music Spatial Lab**.

The window is organized into:

1. Spatial field
2. Direct events / percussion
3. Speaker voicing
4. Geometry / time alignment

Controls include every v0.4 parameter above plus the existing speaker distances.

### Quick-start presets

Preset buttons only populate the controls. They do **not** alter the installed config/audio until
Edi presses **Apply Live**.

**Natural**
- restrained width
- stronger direct-event rejection
- high-frequency ambience emphasis
- darker rear LP
- lower center sparkle

**Wide**
- wider base side feed
- moderate adaptive ambience
- less-dark rear voicing
- still no v0.2-style common-program rear copy

**Ambient**
- highest adaptive ambience
- small base side width
- strongest high-band emphasis
- slow release
- aggressive onset rejection
- darker rear band

**v0.3**
- restores the v0.3 reference parameters.

**Reload Saved** discards unsaved UI experiments and reloads the installed config.

**Apply Live** keeps the existing behavior:
1. update managed config keys,
2. send `reload` to the persistent daemon,
3. rebuild the SURROUND pipeline,
4. keep the background engine process alive.

## Upgrade behavior

Day-to-day installer preserves every OHL Music key already present in Edi's installed config.

New v0.4 keys are appended only when absent:

- ambience low/mid/high weights
- ambience attack/release
- direct threshold/recovery
- center LP
- rear LP
- SL/SR trims

This is intentional because Edi already tuned v0.3 by ear.

## Tests added / validated

Run #97 includes the previous v0.3 behavioral suite plus v0.4 tests proving:

- ambience band weights can enable/disable extraction from the same high-frequency spatial signal,
- ambience attack time materially changes how quickly surrounds open,
- SL/SR trim values independently scale the rear channels,
- rear low-pass can intentionally darken high-frequency surround detail,
- center low-pass bounds the center sparkle band.

The original v0.3 invariants still pass:

- no synthesized rear from centered mono,
- subtle side information still widens,
- diffuse anti-phase information opens strongly,
- clap onset rejection works,
- sustained ambience returns,
- FL/FR are preserved,
- LFE remains silent,
- measured speaker alignment remains intact.

## Validation state

OHL Music v0.4 is **CI-validated, hardware/listening validation pending**.

Do not call v0.4 hardware-validated until Edi installs run #97 and listens on the STR-K900 setup.

## Suggested first tuning workflow

Start from **v0.3 Baseline** if the upgrade preserved an experimental setting that is hard to
interpret.

Then tune by category rather than random knob-chasing:

1. Rear band / trims
   - set SL/SR balance and tonal darkness first.
2. Base side width
   - establish minimum stage extension.
3. Low/Mid/High weights
   - decide what kinds of spatial residue are allowed to drive the room.
4. Adaptive ambience + attack/release
   - set how much and how quickly the room opens.
5. Direct-event reject / threshold / recovery
   - keep main percussion/events forward without killing their tails.
6. Center band/gain
   - add only the useful brightness from the Bose center.

Likely next high-value features after v0.4 hardware tuning:

- live correlation / ambience / onset meters in Spatial Lab,
- temporary A/B snapshots for two tunes,
- named user presets saved to disk,
- optional per-track/application profile switching,
- optional rear extra-delay / depth control only if Edi specifically wants it,
- speaker test tones / channel diagnostics from the same UI.
