# Saikou Switch

This directory is the start of a native Nintendo Switch homebrew port of SaikouTV.

The Android application cannot be compiled directly into an NRO because its UI, lifecycle, storage, networking and media stack depend heavily on Android/AndroidX. The Switch port therefore keeps the application goals and reusable protocol/data ideas while replacing the platform layer.

## Planned layers

- `source/` — Switch-native application code
- libnx — system, input and app lifecycle
- deko3d — GPU/UI rendering
- Switch-native HTTP/JSON — AniList and source APIs
- mpv/FFmpeg integration — video playback, adapted from the known-good SwitchWave work

## Milestone 0

The first target is deliberately small: produce a working `.nro`, initialize libnx, read controller input, and provide a stable application loop. Networking, AniList screens, scraping and playback will be added incrementally after that baseline works.
