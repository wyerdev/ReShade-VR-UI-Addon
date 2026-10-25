# UEVR + ReShade VR Plan

Findings live in `UEVR_RESHADE_RESEARCH.md`. Keep that file short and update
it whenever something new is learned. Record disproven ideas too, not only
successes.

## Current Status (2026-09-26)

Option 1 works. Effects show in both eyes on the Simulator and on a real
Pimax headset in UEVR games (D3D11 and D3D12). Native OpenXR (Hubris) still
works, and the FPS counter with graph works on the headset.
Open: depth effects. ReShade in the game folder (setup A) is not supported;
users must uninstall it there or rename its `dxgi.dll` (see `INSTALL.md`).

## Goal

Get ReShade effects into the headset with UEVR. UEVR is
injected after the game has started, which is what broke the earlier attempts.
Direct3D 11 and Direct3D 12 must both work. OpenXR and OpenVR support must stay.

## Option Order (User Priority)

1. Preferred: one ReShade add-on only.
2. If one add-on is not enough: a UEVR C++ plugin plus the ReShade add-on.
3. Last resort: a custom ReShade build, used only if nothing else can work.

Earlier notes that ruled out any of these options no longer apply.

## Phase 1: Source Research (Done)

- Study `.deps/reshade` and `.deps/UEVR` (codegraph indexes exist in both).
- Answer, for each option, whether it can work and why.
- Output: `UEVR_RESHADE_RESEARCH.md`.
- Status: done. Option 1 was possible and is now built.

## Phase 1.5: UEVR Baseline On The Simulator (Done)

- Test game: Creatures of Ava (Steam app 2304440, Direct3D 12).
- UEVR from `A:\UEVR-build\uevr-reshade-testing`, with our `.deps/UEVR` backend
  copied in by `tools/test_uevr_simulator.ps1 -Action Deploy`.
- `tools/test_uevr_simulator.ps1 -Action Run` starts the injector (OpenXR
  selected), starts the game on the OpenXR Simulator, saves a headset capture
  and logs to `build/uevr-simulator/<game>`, then closes both. Each step has a
  30 second limit; on failure it kills what it started.
- `-KeepRunning` leaves the game up for live checks (simulator MCP, UEVR MCP
  plugin at `localhost:8899`); `-Action Stop` closes it and restores settings.
- Status: passed 2026-09-24. The capture shows both eyes with the UEVR menu.
  ReShade layers were disabled in the game process for this baseline.
- ReShade layer baseline (2026-09-26): official ReShade 6.8.0 installed by the
  user as an OpenXR layer, run with `-EnableReShadeLayers -UseTestReShadeBase`.
  ReShade and our add-on load, and the add-on menu shows in both eyes. The
  image is not inverted: ReShade logs "Skipping OpenXR session because it was
  created without a proxy Direct3D 12 device". The game made its device before
  ReShade loaded, so this is the case Phase 2 must fix.

## Phase 2: Option 1 Prototype (Done)

- Add a fallback to the OpenXR layer. When official ReShade skips the session,
  create a runtime with `reshade::create_effect_runtime` on UEVR's device and
  queue, then apply the effects to the eye images.
- Answer every item in the research note's "Risks And Open Questions".
- Run `build.bat` and the existing Simulator smoke tests to confirm native
  OpenXR still works.
- Then run the UEVR cycle with `-EnableReShadeLayers` and check the capture
  for the test effect in both eyes.
- Move to option 2 only if a blocker appears that the add-on cannot solve.
  Move to option 3 only if option 2 also fails.
- Status (2026-09-26): works on the Simulator. The invert test effect shows
  in both eyes of the UEVR capture; the add-on menu stays un-inverted.
  `build.bat` and the D3D11/D3D12 smoke tests pass. The user confirmed effects
  on a real Pimax Crystal Super with the installed add-on, in Jedi Survivor
  (D3D12) and Jedi Fallen Order (D3D11). Hubris (native OpenXR) still works
  on the headset. ReShade in the game folder (setup A) is not supported.

## Phase 3: Test Tooling (Done)

- Injection uses the nightly `UEVRInjector.exe --attach=<exe>`. Its OpenXR
  option is selected through the window's accessibility interface, because
  the choice is saved per install folder and defaults to OpenVR.
- If extra logging is needed, a temporary UEVR build from `.deps/UEVR` is
  allowed. It is for investigation only and must not be shipped.
- The user's custom UEVR copy under `%APPDATA%` is for inspection only.
  Never edit anything there.
- Run the OpenXR Simulator through `XR_RUNTIME_JSON` in the game process only.
  Do not change the machine-wide OpenXR runtime.
- Use an obvious test effect (for example
  `tools/openxr/DeterministicInvert.fx`) and Simulator captures to check that
  both eyes change.

## Phase 4: Full Test Cycle

1. Build the add-on.
2. Deploy UEVR and run the UEVR cycle script (it starts and closes the game).
3. Read the ReShade, add-on and UEVR logs, and inspect the captures directly.
4. Do a final check on a real headset. Only the user can report what it looks
   like there.

- Status (2026-09-26): passed on the Simulator and on a Pimax headset (UEVR
  Jedi games, native Hubris). The FPS counter and graph work on the headset.

## Decided

- ReShade setup: official ReShade installed as an OpenXR layer (no `dxgi.dll`
  in the game folder). This is the setup to get working first.

- Game: Creatures of Ava. The agent may launch it, only through the UEVR cycle
  script.
- Steam relaunch and its "custom arguments" prompt are avoided by starting the
  game executable directly with `SteamAppId` set (Steam must be running).

## Not Needed From The User Right Now

The codegraph indexes for `.deps/reshade` and `.deps/UEVR` both work.
