# OHL Music v0.2 checkpoint — 2026-09-29

## Source of truth

Repository: `Edi-Gaming/auto-layout-virtual-ac3-encoder`

Branch: `feature/auto-layout`

PR: #1 — keep open; do not merge unless Edi explicitly asks.

The hardware-testable v0.2 code checkpoint is:

`34958d279c7c510993b432b77cb7eb6aef116318`

CI run #75 passed Windows build, unit tests, artifact assembly and upload.

Artifact from run #75:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `11067102079`
- digest: `sha256:a4f92ebc003022f76f9f6f2c6549de2b235e7e9d1e3e6ddbefc3025df76c07bb`

Documentation commits may follow this code checkpoint. Always fresh-fetch the branch HEAD before editing.

## Why v0.1 was changed

First hardware listening feedback from Edi on v0.1:

- `THAT GUY` by Tyler, the Creator had basically no useful spatial effect.
- On `Du riechst so gut`, rear channels felt heavily compressed/gated on transients and could
  effectively disappear around snare hits.
- Niko Rubio's `Feliz Por Conocerte` also produced too little rear contribution.
- Edi wants the rears to at least widen the stage even when there is little decorrelated ambience.
- The physical Bose center being completely silent removed its timbre problem, but Edi noted that
  its brighter sound could be useful selectively for centered treble.
- Settings/UI control is desired; hand-editing the config is no longer the intended tuning workflow.

These observations match the v0.1 math: its rear output depended almost entirely on blockwise
side/correlation analysis, so highly correlated ~32 ms blocks could drive the target near zero.
Transient weighting and relatively quick downward smoothing made the subjective collapse worse.

## v0.2 DSP design

Files:

- `engine/src/OhlMusicUpmixer.h`
- `engine/src/OhlMusicUpmixer.cpp`

FL/FR remain bit-for-bit conceptually preserved from source stereo before speaker-delay alignment.

Rear field is now two-layered:

1. **Permanent decorrelated width bed**
   - same-side L/R-derived information
   - common-mode center mostly suppressed, but not completely removed
   - rear high-pass
   - independent left/right first-order all-pass phase rotation
   - independent of the analyser, so transients cannot gate it off

2. **Adaptive ambience layer**
   - L-R / side energy
   - correlation/coherence weighting
   - channel-balance weighting
   - no crest-factor/transient suppression
   - faster opening, deliberately slow release to avoid pumping

Current reference defaults:

```ini
music_surround_gain=0.78
music_width_floor=0.22
music_center_treble_gain=0.18
music_center_treble_hz=2400
music_rear_highpass_hz=140
```

## Center policy

v0.2 no longer forces physical C to zero.

The physical center receives only:

- highly centered material (high L/R correlation + balance), and
- only the high-passed component above the configured center cutoff.

Intent: use the bright Bose cube as a low-level "sparkle"/intelligibility source, not as the main
vocal speaker. Vocal/body localization should remain primarily phantom-center in FL/FR.

LFE remains zero in OHL Music.

## Time alignment

Measured listening-position distances remain:

- FL 33 in
- C 30 in
- FR 33 in
- SL 27 in
- SR 33 in

At 48 kHz current integer-sample alignment is approximately:

- FL +0
- FR +0
- C +11
- SL +21
- SR +0

## Native multichannel bypass

Unchanged hard rule:

Real activity in the source C/LFE/surround channels above the existing auto-layout threshold
bypasses OHL Music and uses the existing native `enc51_` path.

OHL Music remains an opt-in stereo policy inside `layout=auto`; it does not replace native 5.1.

## Settings UI / live reload

New files:

- `engine/src/MusicSettings.h`
- `engine/src/MusicSettings.cpp`

Access:

- tray -> **OHL Music settings...**
- main OHL mode switcher -> **OHL MUSIC SETTINGS...**
- direct: `engine.exe --music-settings`

Controls:

- Enable/disable OHL Music for stereo
- Adaptive ambience
- Always-on width
- Center sparkle
- Center high-pass cutoff
- Rear high-pass cutoff
- FL/C/FR/SL/SR listening-position distances

**Apply Live**:

1. updates only the managed keys in the installed config,
2. sends `reload` through the existing named control pipe,
3. the daemon reloads config and rebuilds the surround pipeline,
4. the persistent engine process remains alive.

The mode pipe therefore now accepts:

- `surround`
- `guitar`
- `status`
- `reload`

## v0.2 validation state

CI run #75 on code HEAD `34958d279c7c510993b432b77cb7eb6aef116318`:

- configure: success
- Windows Release build: success
- unit tests: success
- artifact assembly: success
- artifact upload: success

New/updated behavioral tests cover:

- measured speaker delay geometry
- untouched FL/FR + silent LFE
- nonzero width bed on highly correlated stereo
- much stronger output for anti-phase ambience than baseline width
- rear field surviving a loud correlated transient
- center-treble path strongly favoring high-frequency centered material
- surround speaker delay after spatial extraction

OHL Music v0.2 is **CI-validated, hardware/listening validation pending**.

Do not call v0.2 hardware-validated until Edi listens to it on the STR-K900 setup.

## First v0.2 listening targets

Re-test:

- Rammstein — `Du riechst so gut`
  - snare should no longer collapse the rear field
  - guitars/attacks should still feel front-anchored
  - rear ambience/width should remain continuous

- Tyler, the Creator — `THAT GUY`
  - should now have at least a persistent widened stage even if correlation analysis finds little
    extractable ambience

- Niko Rubio — `Feliz Por Conocerte`
  - should produce an audible but non-dominant width bed instead of "rears doing fuck all"

Then tune live in this order:

1. Always-on width
2. Adaptive ambience
3. Center sparkle
4. Center HP cutoff
5. Rear HP cutoff

Do not jump to multiband/FFT steering until v0.2's simple continuous-width architecture is judged
on hardware.
