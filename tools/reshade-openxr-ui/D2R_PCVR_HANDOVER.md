# Handover: D2R-PCVR Conflict (Next Session)

Continue the D2R-PCVR conflict work in this repository. Read first:

- [MOD_OWN_RESHADE_CONFLICT.md](MOD_OWN_RESHADE_CONFLICT.md): findings, Option A and Option B.
- `.github\copilot-instructions.md` (repository instructions) and
  `.github\instructions\openxr-ui-workflow.instructions.md`.

## Background (Confirmed 2026-10-23)

- The D2R-PCVR mod (`C:\Progs\Diablo II Resurrected\mods\D2R-PCVR`) loads its own
  ReShade 6.8.0 (`mods\D2R-PCVR\reshade\ReShade64.dll`) into `D2RLoader.exe`.
- Our OpenXR layer attaches to that ReShade. When the game world starts, our effect
  fallback starts a second effect runtime (7952x3924, D3D12), and the game crashes
  about 2 seconds later.
- Without our layer, VR never starts. The central ReShade layer
  (`%ProgramData%\ReShade\ReShade64_XR.json`) refuses to load in `D2RLoader.exe`.
  When no layer loads, the OpenXR loader then fails `xrCreateInstance` with -32.
  Our layer must therefore keep loading in such games.
- The game works when started with `DISABLE_XR_APILAYER_reshade_vr_ui=1` and
  `DISABLE_XR_APILAYER_reshade_1=1` (user-confirmed).
- The mod source (upstream `main`) is in `.deps\d2r-pcvr`, read-only reference.

## Task

Status 2026-10-23: steps 1 to 3 are done (Option A built; see "What Was Built" in
[MOD_OWN_RESHADE_CONFLICT.md](MOD_OWN_RESHADE_CONFLICT.md)); `build.bat` and the
smoke test pass. The user confirmed in D2R on the headset that the game no longer
crashes and the panel controls the mod's ReShade. The `Load subfolders` button
also works in D2R (FakeHDR loads); the panel now shows clearer progress while
the effects reload (retested in D2R: works, under a second with ReShade's cache).

1. Undo or replace the unfinished game-list edits. The user rejected a list of
   games; detection must be automatic. They are in
   `tools\reshade-openxr-ui\reshade_openxr_ui_addon.cpp`:
   - `default_turned_off_programs`
   - `read_turned_off_for_this_program`
   - `s_turned_off_for_this_program`
   - `openxr_ui_is_turned_off_for_this_program`

   The matching declaration is in `vr_ui_state.hpp`. These edits were never
   built.
2. Detect automatically when a game brought its own ReShade. That is any
   ReShade module that is not the central one used by the `XR_APILAYER_reshade`
   layer. The repo's smoke, capture, SOMA and UEVR test scripts load ReShade
   from other folders and must keep passing.
3. Option A: in such games, our add-on must:
   - never start the effect fallback;
   - never pause effects on the mod's "desktop" runtime;
   - point the panel at the mod's ReShade runtime, so its techniques and
     presets show in our panel.
4. Option B, the fallback if Option A cannot work safely: our layer stays a
   pure pass-through. It loads and negotiates normally,
   `xrCreateApiLayerInstance` only calls the next layer, and
   `xrGetInstanceProcAddr` returns the next layer's functions. Log one line
   that explains why.
5. Respect Rule 1: no frame-rate drop and no graphics-card (GPU) or processor
   (CPU) waits.
6. Run `build.bat` until it passes, then the smoke test per the workflow
   instructions.

## Testing In D2R

- The user runs every D2R test; the agent never starts D2R (see "D2R-PCVR
  Tests" in `.github\copilot-instructions.md`). Reaching the game world needs
  clicks in the game window, which the agent cannot do.
- `tools\test_d2r_pcvr.ps1` is a test run for the user. It starts
  `D2RLoader.exe -mod D2R-PCVR` with per-process settings (simulator or headset
  runtime, the build's add-on layer, the installed layer turned off) and saves
  logs to `build\d2r-pcvr\last-run`. Never change the machine-wide OpenXR
  runtime.
- Logs to check:
  - `mods\D2R-PCVR\reshade\ReShade.log`
  - `mods\D2R-PCVR\d2rloader\logs\d2r-vr-frame.log`
  - `mods\D2R-PCVR\d2rloader\logs\d2r-pcvr-vrcam.log`
- Success means:
  - VR starts;
  - the game world runs well past the old crash time (about 2 seconds) with no
    crash;
  - with Option A, our panel lists the mod's techniques (such as
    `D2R_DepthFog`).
- After the user's test, read the logs read-only and ask only for what the logs cannot show.

## Rules

- Keep [MOD_OWN_RESHADE_CONFLICT.md](MOD_OWN_RESHADE_CONFLICT.md) short and
  current with what was built and tested.
- Git is read-only.
- Never edit files in the game folder.
- Ask before deleting anything.
