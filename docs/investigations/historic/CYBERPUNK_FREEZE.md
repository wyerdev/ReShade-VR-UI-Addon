# Cyberpunk 2077 freeze with the effect fallback

Status: fixed and archived. The user confirmed on 2026-11-03 that Cyberpunk VR no longer freezes with the stand-in swapchain (see "Fix built"); diagnosis tools removed 2026-11-04. Left open: a frame rate comparison with and without the add-on, and headset retests of UEVR and BioShock VR.

## Setup

- Game: Cyberpunk 2077 + cyberpunkvrport mod (red4ext), CET, ReShade 6.8.0.2155 (machine-wide), RTX 5090, Direct3D 12, DLSS frame generation (Streamline) hooks active in the mod log.
- The add-on is installed machine-wide (`install.bat`). The user plays this game without the add-on (`uninstall.bat`) with no freeze.
- Official ReShade does not start a VR effect runtime here, so the add-on starts its own "effect fallback" (`openxr_effect_fallback.cpp`): it created a hidden composition swapchain (6720x3384) on the game's command queue, then a ReShade effect runtime on it (before the fix below).

## Symptom

About 2 to 3 seconds after the fallback starts, the game stops: no more `xrEndFrame` calls, the headset image freezes. It happens on every run.

## Facts established (by test or log)

- Freeze is gone with the fallback off (`effect_fallback.bat off`); then no effects are drawn.
- Freeze still happens with the fallback creating only the runtime and drawing nothing (`runtime` stage).
- Freeze still happens with the fallback creating only the hidden swapchain and no effect runtime (`swapchain` stage, confirmed by the log line "hidden swapchain created, no effect runtime"). So effect drawing, effect loading and the ReShade runtime are not needed to cause it.
- Direct3D 12 device removed reason is 0, so it is not a GPU reset.
- The freeze watchdog shows no hang inside our own layer calls. The headset frame thread (the mod's) stops after the second eye's `WaitSwapchainImage`, before it releases.
- The mod's own log (`red4ext\plugins\cyberpunkvrport\log\cyberpunkvrport.log`) shows its headset loop running about 81 per second while the game presents about 1,444 per second, then just stops. It has no error lines about the freeze.
- An earlier stack dump showed the game's main thread spinning in game code called from the mod, with no wait call on top. This suggests it polls something (for example a GPU fence) that is never signalled.

## Not proven

- Why the extra swapchain stops the game. Best guess: DLSS frame generation (Streamline) tracks every swapchain created on the game's queue/device, so a second, never-presented swapchain breaks its frame pacing or fence signalling. Unconfirmed.
- Whether the swapchain must be on the game's queue (try a separate queue or device) or whether any swapchain on that device is enough.

## Fix built (2026-11-03)

Instead of testing guesses one by one (frame generation off, other queue, other format), the fix removes the thing every guess points at: the fallback no longer creates any real DXGI swapchain.

- ReShade's effect runtime only reads a few things from the swapchain it gets: its device, its description, its back buffer, its colour space and some private data (checked in `.deps\reshade\source\addon.cpp`, `d3d11_impl_swapchain.cpp`, `d3d12_impl_swapchain.cpp`). It never presents it.
- `openxr_effect_fallback.cpp` now hands ReShade a small stand-in object (`offscreen_swapchain`) that answers those calls, with a plain texture on the game's device as the back buffer (typeless format, so ReShade can make normal and sRGB views). No DXGI factory call happens, so frame generation, overlays or anything else that hooks swapchain creation never sees a second swapchain.
- The copy-in, effects, copy-out path is unchanged. No new GPU or CPU waits (Rule 1).

Verified so far:
- `build.bat` passes (renderer tests included).
- BioShock VR on the 32-bit simulator (Direct3D 11): the log shows "effect fallback started a ReShade effect runtime for 7124x3562 headset images", and the capture shows the invert effect (only in one eye there; that is the known simulator-only eye layout issue in `BIOSHOCK_32BIT.md`, not new).
- Direct3D 12 path: verified only by Cyberpunk on the headset (below); no local Direct3D 12 game test runs the fallback.

Proven by the Cyberpunk test: a real DXGI swapchain on the game's device caused the freeze, and the stand-in avoids it. Still not proven: which part of the game reacted to it (frame generation is the best guess).

## Can the fix break other games or mods?

Checked in ReShade's source; low risk, but not every case is tested.

- Only games where the effect fallback runs are affected (official ReShade starts no VR effect runtime: UEVR games, the BioShock VR mod, Cyberpunk VR). Games where official ReShade handles VR never create the stand-in.
- ReShade gets the same answers as before: same size, format and device; no window (the old composition swapchain had none either, `runtime.cpp` line 557 reads it for keyboard input); colour space from the format. It now has one back buffer instead of two, and uses less video memory.
- Possible risk: other ReShade add-ons receive the `init_effect_runtime` event (`runtime.cpp` line 576) and can ask the runtime for its native swapchain. They get the stand-in, which refuses window, fullscreen, output and resize requests and anything newer than `IDXGISwapChain3`. An add-on that assumes those calls succeed could fail; none is known.
- Tested: BioShock VR (Direct3D 11, 32-bit, simulator) and Cyberpunk VR (Direct3D 12, headset). Not retested: UEVR (Creatures of Ava) and D2R-PCVR (the fallback crashed there for another reason, `MOD_OWN_RESHADE_CONFLICT.md`).

## Plan

1. Done: Cyberpunk VR runs without freezing (user test, 2026-11-03).
2. Done (2026-11-04): removed every diagnosis tool, so the shipped add-on has no extra logging, background thread or per-frame registry read: the `DisableEffectFallback` switch and its stages, `effect_fallback.bat`, the freeze watchdog and the trace ring in `openxr_ui_layer.cpp` (that file is back to its previous version). They were never committed.
3. To do: compare the frame rate in Cyberpunk VR with and without the add-on (Rule 1).
4. To do: retest UEVR (Creatures of Ava) and BioShock VR on the headset.

## How the cause was found (for similar problems)

The diagnosis stages showed that a hidden swapchain alone caused the freeze. Reading ReShade's source then showed that ReShade never presents the swapchain it is given, so a stand-in could replace it. One build fixed the problem without testing each guess separately.

## Test tools

- After changing code: `build.bat`, then `install.bat` (build alone does not update the installed add-on).
- BioShock VR (`test_bioshock_vr.bat capture`) is the only local game test that runs the fallback (Direct3D 11, 32-bit). It is not yet fully automatic: the intro screen needs a mouse click, and the script's clicks do not reach the game (see `BIOSHOCK_32BIT.md`). Until fixed, it needs the user to click once.
- Read results from the game's `bin\x64\ReShade.log` (effect fallback lines) and the mod log above.