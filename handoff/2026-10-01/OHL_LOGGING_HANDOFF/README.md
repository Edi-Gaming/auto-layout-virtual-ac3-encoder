# OHL Music logging handoff

## Why this exists

The current decoder finally sounds excellent on hardware. The next priority is **measurement and capture**, not another speculative DSP rewrite.

Edi's latest important observation:

- reducing **global Front Lock** often widens a surprising number of songs,
- but the decoder is already sounding very good,
- therefore the next DSP changes should be driven by synchronized input/output captures rather than guessing.

The goal of the logging work is to let a future ChatGPT session inspect exactly what OHL heard, exactly what OHL rendered, and exactly what the classifier believed at the same sample positions.

## Source of truth

Repository: `Edi-Gaming/auto-layout-virtual-ac3-encoder`

Development branch: `feature/auto-layout`

PR: #1 — keep open and unmerged unless Edi explicitly asks to merge it.

Latest green UI/code checkpoint before this handoff:

`8ea41262cb259b368fb50666f86667a007976e59`

CI run #262: green.

Run #262 artifact:

- `OHL-Music-v0.11.4-Polish-run262.zip`
- SHA-256: `7271a014acb6c003fd85a5de243c6dbf276362679088a2189f738a5344b75e91`

Hardware-good DSP baseline remains v0.11.2. The later v0.11.3/v0.11.4 work is UI/telemetry/control-plane polish.

## Current product state

### DSP

The current direction is working very well in real listening.

Important architecture:

- clean front stage preserved,
- true per-bin spectral routing,
- persistent spectral ownership,
- frequency-dependent steering and Front Lock,
- soft ownership rescue for subtle cues,
- continuous rear width bed,
- transient protection applied to adaptive material rather than chopping the stable bed,
- strict rear-energy budget,
- per-speaker time alignment,
- native 5.1 bypass remains sacred.

Do **not** redesign the DSP before captures exist unless a clear bug requires it.

### UI

Current UI has:

- MIX and LAB pages,
- four MIX macro controls: Ambience, Width, Intelligence, Front Lock,
- interactive listening-stage diagram,
- draggable speaker distances,
- live per-speaker RMS,
- live scene analyzer,
- presets and A/B snapshots,
- no-console launcher,
- antialiased stage / knob / history rendering.

### Important new listening finding

Global Front Lock appears to suppress more than just vocals/direct center content on many modern mixes. Lower values often make the scene significantly wider.

Do not immediately "fix" this by changing defaults. Capture evidence first. The likely future target is:

> lock the source, not the width around the source.

That means distinguishing coherent lead/source energy from coherent-but-spatial instrumentation, stereo production texture, room tails, hats, pads, and reverberant width.

## Next task

Implement the synchronized capture system specified in `LOGGING_PLAN.md`.

Start with the minimal safe implementation:

1. exact stereo input WAV,
2. exact rendered 5.1 output WAV,
3. telemetry CSV,
4. manifest JSON,
5. non-real-time disk writer,
6. UI / named-pipe capture trigger,
7. easy bundle for upload into a new ChatGPT session.

Read `IMPLEMENTATION.md` before editing code.
