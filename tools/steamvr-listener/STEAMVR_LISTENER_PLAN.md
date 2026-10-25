# SteamVR Listener: Future Plan (Not Started)

## Problem

The system-wide SteamVR listener checks the running programs on a timer. Each check costs a small burst of processor time (about 2-3 ms). Users who are limited by the processor notice these bursts. The goal is a listener that does no work at all while no enabled SteamVR game starts.

The listener is a separate, optional download (`ReShade-System-Wide-SteamVR-Listener.zip`). The normal add-on installer does not include it. Neither the listener nor the SteamVR add-on is in the GitHub release for now; SteamVR players use ReShade alone (see `tools/reshade-openxr-ui/INSTALL-SteamVR.md`).

## Idea To Test (Option D)

Instead of checking running programs on a timer, let SteamVR tell the listener when a game connects.

1. Register the listener with SteamVR as a background application, so SteamVR starts it and stops it. The listener then does not run at all when SteamVR is closed.
2. The listener waits for SteamVR's "a program connected" message (`VREvent_ProcessConnected`) or "the active game changed" message (`VREvent_SceneApplicationChanged`). Both include the game's process id.
3. If that game is enabled in ReShade Setup (`%ProgramData%\ReShade\ReShadeApps.ini`), load the central `ReShade64.dll` and then `ReShade-SteamVR-UI.addon64` into it.
4. After loading, make sure ReShade's SteamVR hook is active even though the game already connected (the add-on can ask SteamVR for the compositor interface again).

## Why It May Not Work

ReShade only works if it is inside the game before the game creates its graphics device (its connection to the graphics card). SteamVR's message arrives after the game connects to SteamVR.

- Game creates its graphics device first, then connects to SteamVR: the message arrives too late. ReShade loads, but nothing appears.
- Game connects to SteamVR first, then creates its graphics device: it is a race. The listener has only a few milliseconds to load ReShade.

Only a test on real games can show which order games use.

## Test Plan

1. Build a test version of the listener that uses SteamVR messages instead of the timer.
2. The user enables one 64-bit SteamVR game in ReShade Setup (for example Skyrim VR). The agent does not change ReShade Setup settings.
3. The user starts the game. Check the listener log and the game's `ReShade.log`: did ReShade load in time, and do the effects and the menu appear in the headset?
4. If it works, repeat with a game made with a different engine (for example Half-Life: Alyx or an Unreal game).
5. Measure processor use while idle and while a game starts. Record the largest single burst, not only the average.

## Decision

- Works on both games: replace the timer-based listener with the message-based one.
- Fails: drop the system-wide listener. SteamVR users copy the add-on into each game folder.

## Current State

- The timer-based listener has staged changes that check less often (programs every 500 ms instead of every 25 ms). This lowers the average processor use but not the size of each burst. Keep or revert these once the decision above is made.
