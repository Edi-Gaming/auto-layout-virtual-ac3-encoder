# OHL Music v0.11.3 UI / telemetry checkpoint

## Scope

This checkpoint is a **UI + telemetry/control-plane redesign only** on top of the hardware-good
v0.11.2 DSP.

Do not reinterpret this checkpoint as a new audio algorithm.

Hardware-good DSP baseline:

`db018c57d63f3a4d4db74bf19ab342ee64f58ea1`

v0.11.3 executable/UI checkpoint:

`59a898677d0d1b7295ed2f877d9490bd3fdf77e8`

CI run #252: green.

Artifact digest:

`sha256:ab04c477314f32d219b78b87f9e23a655ddd01521613f24c96acea6cf9347664`

PR #1 remains open and unmerged.

## Product / design decision

The UI had reached a point where the DSP was substantially more mature than the interaction
design. The old single-page lab exposed too many controls simultaneously and made normal listening
feel like operating a service utility.

The design review intentionally resolved a tension between two valid needs:

1. **Listening-first UI:** the default view should expose only controls that are meaningful by ear.
2. **Engineering access:** every detector, steering, voicing and geometry parameter must remain
   available without editing the config by hand.

The resolution is **MIX vs LAB**, not a compromised page containing half of both.

### MIX

MIX is the default listening view.

It contains:

- four large rotary macro controls:
  - Ambience
  - Width
  - Intelligence
  - Front Lock
- quick-start presets
- in-memory A/B snapshots
- a large listening-stage visualization
- live analyzer
- Apply Live / Reload Saved

The four macros are intentionally the only large tuning controls visible during normal listening.

### LAB

LAB holds the engineering controls:

- low/mid/high ambience weights
- spatial selectivity
- broad ambience attack/release
- spectral ownership attack/release
- diffuse threshold
- rear budget
- transient/direct-event rejection
- event sensitivity/recovery
- per-bin routing blend
- Dimension
- Center Width
- center sparkle / center band
- rear band
- SL/SR trims
- per-band steering
- per-band Front Lock
- speaker distance geometry

The listening stage stays visible while LAB is open so geometry never becomes purely abstract.

## Macro controls

New custom Win32 control:

- `MacroKnob.h`
- `MacroKnob.cpp`

The macros are rotary cards rather than horizontal trackbars.

Input:

- vertical drag
- mouse wheel
- arrow keys

They reuse the existing TBM position/range contract so the settings state remains simple.

Ranges:

- Ambience: 0..120%
- Width: 0..60%
- Intelligence: 0..100%
- Front Lock: 0..100%

## Listening stage

The stage is now a first-class control surface rather than decoration.

Features:

- listener/head graphic
- FL / C / FR / SL / SR
- distance-aware speaker placement
- real per-speaker output activity
- live spatial-field visualization
- speaker hover/drag feedback
- **radial speaker dragging changes distance**

The stage sends `OHL_STAGE_DISTANCE_CHANGED` to the parent settings window.
Dragging updates the matching hidden/visible geometry field but does not alter the running pipeline
until Apply Live.

Typed geometry edits also immediately preview on the stage.

## Per-speaker telemetry

`MusicTelemetry` now includes:

- monotonically increasing telemetry sequence
- FL/FR/C/LFE/SL/SR output RMS

After each OHL Music 5.1 packet, `WasapiPassthrough` computes RMS from the actual rendered 5.1
buffer and publishes it.

This is visualization-only and does not feed back into DSP.

## Flicker / telemetry redesign

The old live analyzer could visibly flicker because a single short named-pipe timeout posted an
offline/zero frame.

v0.11.3 changes that behavior:

- worker polls independently from the UI thread
- packet sequence suppresses duplicate redraws
- short pipe misses retain the last valid telemetry
- offline state is posted only after a sustained outage
- stage and analyzer update only on new engine packets
- stage speaker RMS is display-smoothed with fast attack / slower decay

This intentionally makes the display responsive without using UI smoothing to alter any DSP state.

## Live analyzer

The wide analyzer now uses a dashboard layout:

- two three-meter columns
- band ownership / center confidence
- scrolling history
- human-readable scene interpretation

Examples of scene interpretations:

- direct transient — front protected
- centered lead — front anchored
- air / room field — surrounds breathing
- wide spatial scene — rear field active
- front-heavy mix — surrounds restrained
- subtle spatial cues — light rear support

## Invariants

Do not regress these without a specific listening reason:

1. v0.11.2 remains the audio baseline.
2. MIX must remain listening-first; do not repopulate it with expert fields.
3. LAB must retain full tuning access.
4. Stage visualization is driven by actual output telemetry, never by fake UI animation.
5. Named-pipe misses must not flash telemetry to zero.
6. Speaker dragging edits geometry only; it never silently auto-applies/reloads audio.
7. PR #1 stays open until Edi explicitly asks to merge.

## Validation

Run #252 passed:

- installer PowerShell parser
- Windows Release build
- full unit / DSP suite
- portable artifact assembly
- artifact upload

Hardware/UI visual validation is pending Edi installing run #252.
