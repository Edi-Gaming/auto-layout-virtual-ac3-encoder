# Paste this into a fresh chat

Continue my OHL / Virtual AC3 Encoder project from GitHub.

Repository:

`Edi-Gaming/auto-layout-virtual-ac3-encoder`

Do **not** use `main` as source of truth.

Current development branch:

`feature/auto-layout`

Open PR:

#1 — **Add automatic AC3 2.0 / 5.1 layout, runtime modes, and tray control**

Do not merge PR #1 unless I explicitly ask.

Before doing anything else:

1. Verify the current remote HEAD of `feature/auto-layout`.
2. Read `START_HERE.md`.
3. Read `handoff/2026-09-24/OHL_FRESH_CHAT_HANDOFF_20260924.md` in full.
4. Read `handoff/2026-09-24/VALIDATION_AND_RECOVERY.md`.
5. Read the current `README.md`.
6. Then inspect whatever source files are relevant to my next request.

Important immediate state from the previous chat:

- The last code-changing commit before the handoff was `4143e389163feb2d20bb8b2c6344eae1304e0d23` (**Detach hidden daemon from console**).
- CI was green.
- The previous installed build left a console window open at reboot; closing it killed the encoder.
- The latest code changes startup to a one-shot hidden `OHL-Autostart.vbs` launcher and makes `engine.exe --hidden` call `FreeConsole()`.
- That final startup fix was CI-validated but had **not yet been reboot/hardware-validated** when the handoff was written.
- The next real-world test is to install the newest green artifact, confirm no permanent console, test the new **Start OHL Encoder** shortcut, and reboot-test autostart.
- Automatic AC3 2.0/5.1, tray control, OHL branding, and SURROUND/GUITAR S/PDIF handoff were already hardware-validated.
- Keep continuous AC-3 at 48 kHz; do not push hybrid PCM unless I reopen that idea.
- Checkpoint frequently and write important state back to GitHub so the next chat does not depend on chat history.
