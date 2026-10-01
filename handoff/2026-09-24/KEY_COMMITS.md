# Key commits — OHL / Virtual AC3 Encoder

This is not every commit. It is the shortest useful recovery map.

## Latest startup chain

- `f8ded3528ff22d74114bd881d1b1d2c26de2d6fc` — Document hidden one-shot OHL startup launcher
- `4143e389163feb2d20bb8b2c6344eae1304e0d23` — Detach hidden daemon from console
- `e8e094718d342e4b4e66d1afab43e0a8ed0aa75e` — Use one-shot hidden launcher for autostart
- `bd90f7c6275534314b56b7ec7db0dffe9ff0a154` — Hide logon console with one-shot OHL launcher

## Legacy-autostart migration

- `012c4c67c92fd19912e5a384a361eaffb9caecbb` — Document authoritative OHL startup migration
- `05927aa0a814c4b0728f46d295bf2573172714f8` — Stop both legacy and OHL engine paths
- `384863fdc313771771df82b51fa7338f5c204686` — Make packaged installer use OHL direct startup path
- `6a688a124cfb3ff560a9b9253d38030f868b2d6a` — Remove legacy and OHL autostart paths together
- `d048f530d5b37d48b1ca7befe220e3e9e70e5796` — Use direct OHL Startup shortcut for autostart
- `76e817449994ab12b9d61184920d5a2cb5222a07` — Verify authoritative OHL startup target
- `c6f2a9e72e2c23ea0c78ee631e69e2278e593ca5` — Migrate legacy autostart to one authoritative OHL startup path

## OHL branding / switcher

- `4f6938f60ac7f18b2947a894bfa046569a810ba0` — Document branded tray UI and one-shot startup architecture
- `401195c8cb0004fe416dc4559fc2556135f5352b` — Use WNDCLASSEX for branded small window icon
- `c75477de2eafe696c4a38c222a31ecc333210e3c` — Build embedded OHL branding into engine
- `93c9e819febd8e3283756519e9511beb47f0bd59` — Polish mode switcher and fix literal newline rendering
- `c30a2d77b8601e63fa8761d048f83128eb2fb25d` — Use OHL logo for notification-area icon
- `9b640b7340a0dbb1d9e7511d4ff9bc7f71d738ab` — Use compact crop of actual OHL logo for tray icon

## Portable/day-to-day reliability

- `a570324082379d7ee1fa8f0b763ddc6926f42132` — Launch day-to-day daemon directly instead of through WScript
- `2be7c35aa6c1e2f2f57f2b50c212197aee3e8c91` — Preflight engine before hidden daemon startup
- `38cb374e576e2237d09312451f1aef0af2383402` — Ship MSVC runtime with day-to-day build
- `6318eae9cba8087180eb106698374ac3cb0d0b4f` — Make day-to-day installer path resolution robust

## Historical note

There are older supervisor/watchdog-related commits in branch history. Do not infer that they describe the current architecture. Current normal OHL startup is a **one-shot hidden launcher plus persistent engine**, not a restart supervisor.
