# Logging implementation handoff

## Do this next

Implement a new capture subsystem with minimal impact on the hardware-good DSP.

Suggested files:

```
engine/src/MusicCaptureLogger.h
engine/src/MusicCaptureLogger.cpp
```

Add them to both targets only where needed.

## Suggested class shape

Conceptually:

```cpp
class MusicCaptureLogger {
public:
  enum class State { Idle, Armed, Recording, Complete, Saving, Saved, Cancelled, Error };

  bool Arm(const CaptureRequest& request, const CaptureMetadata& metadata);
  void PushPacket(const float* stereo,
                  const float* out51,
                  size_t frames,
                  const MusicTelemetrySnapshot& telemetry);
  void Cancel();
  CaptureStatus Status() const;

private:
  // preallocated audio + telemetry buffers
  // atomic state / frame counters
  // writer thread
};
```

The exact API can differ. Preserve the invariants below.

## Hook location

The best hook is the OHL Music 5.1 path in `WasapiPassthrough.cpp`.

Current logical order is approximately:

```
prepare stereo packet
musicUpmixer_.ProcessStereo(...)
publish telemetry
encode/output AC3
```

Logging needs exact input and output around `ProcessStereo`.

Recommended:

```
logger.BeginPacketReference(stereo, frames); // or capture input locally
musicUpmixer_.ProcessStereo(stereo, frames, music51_.data());
logger.PushPacket(stereo, music51_.data(), frames, telemetrySnapshot);
```

The logger must see the exact stereo buffer passed into the upmixer and exact six-channel buffer passed onward to AC3.

## Telemetry snapshot

Do not read a changing set of atomics repeatedly on the writer thread and assume they all belong to one packet.

Prefer a plain packet snapshot struct created immediately after processing:

```cpp
struct MusicTelemetrySnapshot {
  uint64_t sequence;
  float ambience;
  float center;
  float spatialBins;
  float transient;
  float rearOpen;
  float frontLock;
  float rearBudgetScale;
  std::array<float, 6> speakerRms;
  std::array<float, 4> ownership;
  std::array<float, 4> bandCenter;
};
```

Use the same snapshot both for live telemetry publication and capture logging if practical.

## WAV writer

Implement a small deterministic float32 WAV writer.

Requirements:

- stereo input,
- 5.1 output,
- 48 kHz,
- float32,
- little endian,
- correct frame counts,
- correct header sizes,
- WAVE_FORMAT_EXTENSIBLE for 5.1 if practical,
- no normalization.

Add tests that reopen the WAV header or parse bytes and verify:

- sample rate,
- channel count,
- format,
- data size,
- sample order.

## Capture directory

Default destination should be inside the installed OHL directory or a dedicated user-writable subdirectory such as:

```
<InstallDir>\captures\
```

Do not require admin rights.

Sanitize all generated names.

## Config snapshot

Capture the current runtime config when arming.

At minimum include all `music_*` settings plus:

- source/output device names,
- layout mode,
- bitrate,
- sample rate,
- active speaker distances,
- processing latency.

Do not log unrelated personal/system information.

## Named-pipe integration

Extend existing mode-control pipe rather than creating another IPC system.

Commands:

- `capture start [seconds]`
- `capture status`
- `capture cancel`
- `capture latest`

Responses should be compact and machine-readable.

## UI integration

Keep the MIX UI clean.

Add one compact capture control near A/B or the bottom status strip.

Recommended first iteration:

```
[ CAPTURE 15s ]  READY
```

During capture:

```
[ CANCEL ]  RECORDING 08.4 / 15.0 s
```

After writer completes:

```
[ CAPTURE 15s ]  SAVED — 20261001-191530_ab12cd34
```

Do not make capture auto-start when OHL starts.

## Tests

Add unit tests for:

1. exact input sample copy,
2. exact 5.1 output sample copy,
3. stereo/5.1 frame counts remain equal,
4. telemetry row frame indices are monotonic and packet-aligned,
5. logger stops exactly at target frame count,
6. cancel path,
7. float WAV headers,
8. manifest contains build/config/latency,
9. no capture changes audio buffers,
10. logger idle path is effectively a no-op.

If possible, test that `PushPacket` performs no allocations once recording starts.

## Validation discipline

Before editing:

1. verify current `feature/auto-layout` HEAD,
2. fetch the exact current files,
3. keep PR #1 open,
4. do not touch DSP equations,
5. run CI after each meaningful checkpoint,
6. package only a green exact tree.

After building:

- first hardware test should verify **no dropouts / relocks / CPU spikes** while capturing,
- then record a known-good 10–15 second passage,
- upload the resulting bundle in ChatGPT,
- analyze before changing Front Lock or ownership logic.

## Explicit non-goals for the first pass

Do not add yet:

- continuous always-on disk logging,
- automatic song identification,
- ML source separation,
- AC3 decode round-trip capture,
- large GUI redesign,
- automatic DSP retuning,
- pre-roll ring buffer.

Those can come after the exact in/out path is proven.
