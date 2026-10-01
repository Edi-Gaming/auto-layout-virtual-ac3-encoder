# Paste this into the next chat

We are continuing the OHL / Virtual AC3 Encoder project in:

`Edi-Gaming/auto-layout-virtual-ac3-encoder`

Use the GitHub connector. **Do not use main as source of truth.**

Development branch:

`feature/auto-layout`

PR #1 stays open and must NOT be merged unless I explicitly ask.

Read this handoff package first:

- `handoff/2026-10-01/OHL_LOGGING_HANDOFF/README.md`
- `handoff/2026-10-01/OHL_LOGGING_HANDOFF/LOGGING_PLAN.md`
- `handoff/2026-10-01/OHL_LOGGING_HANDOFF/IMPLEMENTATION.md`

Current known-good pre-handoff code checkpoint:

`8ea41262cb259b368fb50666f86667a007976e59`

CI run #262 was green.

The decoder currently sounds **very good on real hardware**. Do not start by redesigning DSP.

Important listening observation: turning global Front Lock down often widens a lot of songs. We suspect Front Lock may be suppressing some coherent-but-spatial material, but we specifically want evidence before touching it.

NEXT TASK:

Implement synchronized debug capture so we can upload a bundle and analyze exactly what OHL heard and rendered.

Need:

1. exact 48 kHz float32 stereo input WAV immediately before OHL Music processing,
2. exact 48 kHz float32 5.1 output WAV immediately after OHL Music processing / before AC3 encoding,
3. packet-aligned telemetry CSV,
4. manifest JSON containing build SHA, runtime config, processing latency, speaker geometry and channel order,
5. no blocking disk I/O or allocations in the real-time audio path after capture begins,
6. named-pipe capture start/status/cancel commands,
7. a compact UI capture button,
8. tests,
9. green CI artifact.

Keep the current DSP untouched unless the logging implementation exposes an actual bug.

Before any code mutation, fresh-verify branch HEAD and fetch the exact files you will modify.

When the capture build is ready, give me the exact commit SHA, CI run, artifact, hash, and hardware validation limits.
