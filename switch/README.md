# Saikou Switch

This directory contains the native Nintendo Switch implementation of SaikouTV. The Android application remains maintained separately upstream; this app uses libnx and Borealis.

## Current milestone: controller-first Home shell

The app starts directly in a static Home screen with Continue Watching, Trending preview cards, and Latest Episodes preview cards. D-pad focus and A-button actions are wired for the navigation items and cards. Navigation destinations show a clear placeholder message until their screens are built.

The preview titles are UI sample content. No API request runs at startup, so network availability cannot block the first screen.

The GitHub Actions workflow builds the checked-in C++ and XML files as an NRO. It pins Borealis to the revision recorded in the workflow and does not rewrite application source during the build.

## Next milestones

1. Confirm the Home shell on the Switch and tune its layout.
2. Build actual Search, Library, and Settings screens.
3. Connect one Home row to AniList data.
4. Add images and playback only after the UI foundation is stable.
