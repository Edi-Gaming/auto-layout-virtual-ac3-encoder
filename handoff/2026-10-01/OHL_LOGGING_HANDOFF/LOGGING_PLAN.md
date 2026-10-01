# OHL synchronized input/output logging plan

## Objective

Create a debug capture that lets us analyze OHL like an audio engineer instead of tuning by anecdote.

A capture must answer four questions at the same time:

1. **What stereo samples entered OHL?**
2. **What six-channel samples left OHL?**
3. **What did the classifier / router believe for each packet?**
4. **What exact settings/build/latency produced that result?**

The capture must be sample-synchronized and must not perform blocking disk I/O on the real-time audio thread.

---

## Capture bundle

Each capture creates a folder like:

```
captures/
  20261001-191530_ab12cd34/
    input_stereo.wav
    output_5p1.wav
    telemetry.csv
    manifest.json
    README.txt
```

Optionally add a helper script later to ZIP the folder for easy ChatGPT upload.

### input_stereo.wav

Exact stereo float samples immediately before OHL Music processing.

Format:

- 48 kHz
- 2 channels
- IEEE float32
- channel order: L, R
- no normalization
- no gain changes
- no resampling
- no analysis filtering

Hook point: immediately before `musicUpmixer_.ProcessStereo(...)`.

### output_5p1.wav

Exact six-channel float samples after OHL Music processing and before AC3 encoding.

Format:

- 48 kHz
- 6 channels
- IEEE float32 / WAVE_FORMAT_EXTENSIBLE preferred
- channel order: FL, FR, C, LFE, SL, SR
- speaker mask should match that order if practical
- no normalization
- no downmix
- no codec round-trip

Hook point: immediately after `musicUpmixer_.ProcessStereo(...)`, using the exact `music51_` buffer that is then sent to the AC3 encoder.

This is the most important file. It lets later analysis isolate:

- front preservation,
- center behavior,
- rear energy,
- left/right rear asymmetry,
- rear spectral content,
- vocal leakage,
- hat/transient chopping,
- time alignment,
- width behavior,
- budget behavior.

### telemetry.csv

One row per rendered AC3 music packet.

At 48 kHz and 1536 frames per AC3 packet, rows occur every ~32 ms.

Required columns:

```
sequence
capture_frame_start
engine_frame_counter
ambience
center_confidence
spatial_bin_fraction
transient_confidence
rear_open
front_lock_confidence
rear_budget_scale
speaker_rms_fl
speaker_rms_fr
speaker_rms_c
speaker_rms_lfe
speaker_rms_sl
speaker_rms_sr
ownership_low
ownership_body
ownership_presence
ownership_air
center_low
center_body
center_presence
center_air
```

Also record any additional metrics that are cheap and already available.

### manifest.json

Required fields:

```json
{
  "format_version": 1,
  "build_sha": "...",
  "branch": "feature/auto-layout",
  "capture_start_utc": "...",
  "sample_rate": 48000,
  "input_channels": ["L", "R"],
  "output_channels": ["FL", "FR", "C", "LFE", "SL", "SR"],
  "captured_frames": 0,
  "requested_seconds": 15,
  "processing_latency_samples": 512,
  "processing_latency_ms": 10.6667,
  "stereo_processing": "music",
  "config_snapshot": {
    "...": "all music_* keys"
  }
}
```

Important:

- If `music_per_bin_routing=0`, processing latency may be 0.
- Otherwise record the actual runtime latency reported by the upmixer.
- Record speaker distances and trims explicitly even though they are also config keys.
- Store the exact active config, not installer defaults.

### README.txt

Human-facing reminder:

- what the files contain,
- output channel order,
- whether processing latency is included,
- how to upload/analyze the bundle,
- build SHA.

---

## Real-time safety

### Rule: no disk I/O on the audio thread

Do not call:

- `WriteFile`,
- `fwrite`,
- WAV header updates,
- ZIP compression,
- filesystem creation,

from the audio render/capture callback.

### Safe initial design

Use a `MusicCaptureLogger` with a preallocated capture buffer.

When the user arms a capture:

1. Control thread allocates all required memory for the requested maximum duration.
2. Logger changes state to `Armed`.
3. On the next music packet, audio thread changes state to `Recording`.
4. Audio thread only performs bounded `memcpy` / fixed-struct writes into already allocated memory.
5. When requested frame count is reached, audio thread atomically changes state to `Complete`.
6. Writer thread notices `Complete` and writes WAV/CSV/JSON in the background.
7. Writer thread changes state to `Saved`.

No heap allocations inside `PushPacket`.

### Suggested maximum

Initial implementation:

- default 15 s,
- selectable 5 / 15 / 30 s,
- hard maximum 60 s.

Approximate float32 memory for 15 s at 48 kHz:

- input stereo: ~5.8 MB,
- output 5.1: ~17.3 MB,
- total audio: ~23 MB,
- telemetry negligible.

This is trivial on the target PC and safer than streaming file writes from the real-time path.

---

## Synchronization

The input and output files must use the **same capture frame counter**.

For each packet of N frames:

1. Copy N stereo frames into input capture at frame index F.
2. Run OHL.
3. Copy N rendered 5.1 frames into output capture at frame index F.
4. Store telemetry row with `capture_frame_start=F`.
5. Advance F by N.

Do **not** manually time-shift output samples in the file.

The output contains OHL's real processing latency. Record that latency in the manifest so analysis can align input and output exactly.

This is valuable because front-channel preservation and rear extraction can then be measured using the real causal system rather than an artificially shifted render.

---

## Control surface

### Named-pipe commands

Add commands such as:

```
capture start 15
capture status
capture cancel
capture latest
```

Possible status response:

```
capture_state=recording;frames=184320;target=720000;seconds=3.84
```

When saved:

```
capture_state=saved;path=C:\...\captures\20261001-191530_ab12cd34
```

### UI

MIX should get one compact debug action, not a new wall of logging controls.

Suggested:

- button: `CAPTURE 15s`
- small state text:
  - READY
  - RECORDING 6.2s / 15s
  - SAVING
  - SAVED
- optional later button: `OPEN CAPTURE`

LAB may expose duration choices later.

Logging must be **off by default** and require explicit user action.

---

## Phase 2: pre-roll / event marking

Do not block the initial implementation on this, but it will be extremely useful.

Desired later behavior:

- maintain a small in-memory rolling ring buffer,
- user hears an interesting failure,
- presses CAPTURE,
- bundle includes e.g. 3–5 seconds before the click plus 10 seconds after it.

This is especially valuable for intermittent:

- vocal leakage,
- hat chopping,
- ownership dropouts,
- sudden scene collapse,
- over-aggressive Front Lock.

Implement only after the basic exact capture path is proven glitch-free.

---

## Derived analysis files

Do not alter the exact source files, but an offline helper may generate:

```
rear_pair.wav      # exact SL/SR extracted as stereo
front_pair.wav     # exact FL/FR extracted as stereo
center.wav         # exact C
analysis.json      # derived metrics
```

These are convenience files only.

Potential offline measurements:

- input L/R correlation by time/frequency,
- output rear/front RMS ratio,
- SL/SR asymmetry,
- rear vocal-band coherence,
- transient envelope preservation,
- per-band rear energy,
- ownership vs actual rear output,
- Front Lock confidence vs lost width,
- cross-correlation to find leaked copies of front material in rears,
- latency verification,
- rear spectral continuity around hats/cymbals.

The exact WAVs remain the authoritative capture.

---

## What a future ChatGPT session should do with a capture

When Edi uploads a capture bundle:

1. Read `manifest.json`.
2. Read `telemetry.csv`.
3. Load input and output WAVs.
4. Align analysis using `processing_latency_samples`.
5. Plot / calculate:
   - input M/S energy,
   - coherence,
   - per-band energy,
   - output FL/FR/C/SL/SR RMS,
   - rear/front ratio,
   - SL/SR asymmetry,
   - telemetry overlay.
6. Focus on the passage Edi says sounds wrong or especially good.
7. Use the evidence to propose the **smallest** DSP change.
8. Never infer that "more rear energy" is automatically better.

The purpose is not merely to "listen to the files." It is to correlate subjective listening with the actual decoder decisions and rendered channels.
