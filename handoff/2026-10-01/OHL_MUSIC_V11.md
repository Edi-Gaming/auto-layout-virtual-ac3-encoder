# OHL Music v0.11 checkpoint — adaptive matrix lab

## Source of truth

Repository: `Edi-Gaming/auto-layout-virtual-ac3-encoder`

Branch: `feature/auto-layout`

PR: #1 — keep open; do not merge unless Edi explicitly asks.

Hardware-testable executable checkpoint:

`bb20ee7ba9b1f4c4841b241febecaaed757c99b3`

CI run #208: **green**

- PowerShell installer parser
- Windows configure/build
- full unit/DSP suite
- portable/day-to-day assembly
- artifact upload

Artifact:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `11142186443`
- SHA-256: `8719d9e16520eb0c15b8b2b78af2697a487387e17f959a819fc7117b3d669240`

The downloaded ZIP was independently hashed and matched GitHub exactly.

## Hardware baseline

v0.10 is the hardware-proven baseline.

Edi's v0.10 verdict:

> "that is the best its benm. this is MUCH more like pl ii"

v0.11 therefore follows one architectural rule:

**Do not disturb the clean v0.10 front stage or reintroduce nonlinear rear waveform surgery. Add intelligence upstream / in the spectral rear router.**

Rejected techniques remain rejected:

- v0.6 raw/outside-band program copy to rears
- v0.7 over-sparse rear extraction
- v0.8 sample-wise `min(abs(L), abs(R))` common subtraction
- any sample-wise nonlinear carving of rear waveforms

## v0.11 feature set

### 1. True streaming per-bin rear routing

New module:

- `engine/src/OhlSpectralRouter.h`
- `engine/src/OhlSpectralRouter.cpp`

FFT geometry:

- sample rate 48 kHz
- FFT 512
- hop 256
- sqrt periodic Hann analysis/synthesis
- 50% WOLA
- true causal processing latency: **512 samples / ~10.67 ms**

The router works directly on the M/S side spectrum. Exact mono/center therefore produces no spectral rear source.

Per-bin classification includes:

- temporal L/R coherence
- interchannel phase sign
- side fraction
- L/R balance
- hard-pan confidence
- spectral flux / transient confidence
- persistent spatial ownership

### 2. Per-bin ownership hysteresis

Each spectral bin has persistent ownership state.

Default:

- acquire 65 ms
- release 520 ms
- acquire threshold from `music_spatial_bin_threshold`
- release threshold = ~62% of acquire threshold

Silence actively releases ownership; bins cannot remain permanently owned through gaps.

### 3. Four frequency steering bands

Bands:

- LOW: <250 Hz
- BODY: 250 Hz–2 kHz
- PRESENCE: 2–6 kHz
- AIR: >6 kHz

Default rear steering:

```ini
music_steering_low=0.18
music_steering_body=0.55
music_steering_presence=0.90
music_steering_air=1.10
```

This intentionally lets ambience / production air open more than low-frequency program body.

### 4. Frequency-dependent Front Lock

Default multipliers:

```ini
music_front_lock_low=0.30
music_front_lock_body=1.00
music_front_lock_presence=0.82
music_front_lock_air=0.25
```

Global Front Lock still exists and multiplies these values.

Goal:

- strongest front anchoring in vocal/body region
- moderate presence anchoring
- much more freedom for high-frequency room / air
- minimal low-frequency rear intelligence

### 5. Per-bin Routing blend

```ini
music_per_bin_routing=0.55
```

- 0.0 = exact v0.10 broadband renderer fallback; no spectral-router latency
- >0 = blend known-good broadband M/S renderer with true per-bin spectral rear router
- 1.0 = spectral rear router only

When enabled, the broadband rear path is delayed to match the spectral path before blending.
FL/FR/C/LFE receive the same 512-sample processing compensation, preserving alignment.

### 6. Dimension

```ini
music_dimension=0.00
```

Range:

- -1.0 = front-biased
- 0 = neutral
- +1.0 = rear-biased

Dimension scales both the broadband and per-bin rear field. The router test confirms a strong front-to-rear gain range.

### 7. Center Width

```ini
music_center_width=1.00
```

- 1.0 = v0.10 hardware-proven phantom-center front stage, untouched
- moving toward 0 transfers a controlled amount of coherent center energy out of FL/FR and into C

This is a real transfer, not merely adding a duplicate center.

Default remains 1.0 because the user's front stage is already excellent and the physical Bose center is unusually bright.

### 8. Live analyzer telemetry

New `MusicTelemetry.h`.

Engine publishes:

- ambience confidence
- center confidence
- active spatial-bin fraction
- transient confidence
- broadband rear-open amount
- front-lock confidence
- rear-budget scale
- per-band ownership (LOW/BODY/PRES/AIR)
- per-band center confidence

Existing named pipe adds read-only command:

`metrics`

Spatial Lab polls it every 300 ms.

### 9. Spatial Lab right-hand analyzer pane

The settings window is widened.

Right-hand pane includes:

- live headline metrics
- per-band ownership / center table
- Per-bin Routing slider
- Dimension slider
- Center Width slider
- ownership acquire/release
- four per-band steering values
- four per-band Front Lock values

### 10. A/B snapshots

Spatial Lab now provides:

- STORE A
- A ▶
- STORE B
- B ▶

Snapshots are in-memory for the current settings process.

Recall:

1. restores the snapshot into controls,
2. writes it to installed config,
3. applies it live through the existing reload path.

This makes ear-based comparisons fast enough to use on one repeated music passage.

## Rear-budget redesign

The v0.11 spectral path exposed that a packet-average RMS limiter could hide a stronger steady tail behind the initial spectral acquire ramp.

The final limiter now uses:

- a front reference delayed by the exact spectral processing latency,
- overlapping 512-sample RMS windows with 256-sample hop,
- the maximum rear short-window RMS,
- a front RMS envelope with slow release (at least 350 ms, otherwise ambience release).

This preserves legitimate reverb tails while enforcing the configured rear ceiling against a time-aligned recent front level.

## Tests / invariants

Run #208 passes the complete suite.

Important covered behavior:

- v0.10 fallback remains available at `perBinRouting=0`
- v0.10 clean M/S rear superposition / linearity remains valid
- spectral analyzer detects quiet spatial information behind a loud center
- hard pan is distinguished from balanced diffuse information
- spectral directionality follows genuine asymmetric ambience
- spectral router latency is exactly 512 samples
- exact mono produces no routed rear
- per-band steering only opens the enabled frequency band
- Dimension materially shifts rear level
- body-band Front Lock strongly suppresses coherent widened body content
- air is not killed by body-band Front Lock
- ownership releases through silence
- v0.11 fronts are sample-exact after the known 512-sample processing latency
- Center Width 1.0 preserves phantom front stage; lower values transfer coherent center into C
- hard rear budget remains enforced on the per-bin path

## Defaults

```ini
music_spectral_intelligence=0.90
music_spatial_bin_threshold=0.30
music_per_bin_routing=0.55
music_spectral_acquire_ms=65
music_spectral_release_ms=520
music_dimension=0.00
music_center_width=1.00

music_steering_low=0.18
music_steering_body=0.55
music_steering_presence=0.90
music_steering_air=1.10

music_front_lock_low=0.30
music_front_lock_body=1.00
music_front_lock_presence=0.82
music_front_lock_air=0.25
```

Existing installs keep user-tuned old controls and receive the new v0.11 keys only when absent.

## Validation state

v0.11 is **CI-validated, hardware/listening validation pending**.

Do not call v0.11 hardware-validated until Edi listens to run #208.

First listening target:

- leave Center Width at 100%
- leave Dimension at 0
- leave Per-bin Routing at 55%
- compare the same songs where v0.10 alternated between "very PLII-like" and "almost no surround"

Use live ownership meters to see whether the router is finding spatial bands when the rear field opens.

If v0.11 causes any degradation to the excellent front stage, set Per-bin Routing to 0 and Center Width to 100%; that should return the v0.10 fallback path.
