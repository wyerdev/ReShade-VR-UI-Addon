---
description: "Use when building, testing, capturing screenshots, or visually validating the ReShade VR UI Add-on and OpenXR Simulator workflow."
applyTo: "tools/reshade-openxr-ui/**"
---

# ReShade VR UI Add-on Workflow

## Simulator Boundary

- The only programs the agent may start are the OpenXR Simulator test programs run by `smoke.ps1` and `capture_simulator_ui.ps1`. Never start a game, game launcher, mod launcher or injector; the user runs every game test (see `.github/copilot-instructions.md`).
- Use the existing isolated OpenXR Simulator bundle supplied with `-TestRoot`. Do not rebuild, install, register, or modify it unless the task specifically targets the simulator and the user approves that scope.
- Never deploy to Hubris or modify runtime registration as part of simulator validation.

## Command Invocation

- Use Windows PowerShell and checked-in scripts; do not assume a Unix shell is available.
- In a VS Code task, pass `-NoProfile`, `-ExecutionPolicy`, `Bypass`, `-File`, the script path, and script arguments as separate arguments.
- Use a unique task label when command or arguments differ from an existing task.
- Do not put complex validation logic in nested PowerShell quoting; add or use a checked-in script instead.

## Visual Validation

- The simulator test folder on this machine is `..\OpenXR-Simulator-test` (next to this repository); pass it as `-TestRoot`. Its preview window keeps the eye image's shape (black bars, no stretching); if a capture ever looks stretched or cut off, fix the simulator, never note it as a known quirk.
- WARP validates interaction commands and state transitions only; never use debug prints, text-width measurements or hit-test results to decide whether UI text or controls overlap. The exception is `preview_ui.ps1`: its images are the real panel texture drawn by the add-on's renderer, and they are the main tool for design iteration.
- Visual acceptance at the end requires actual OpenXR Simulator captures produced by `capture_simulator_ui.ps1`:
  - `build\reshade-openxr-ui\simulator-captures\d3d11\main.png`
  - `build\reshade-openxr-ui\simulator-captures\d3d11\fps-target-open.png`
  - `build\reshade-openxr-ui\simulator-captures\d3d11\detail.png`
  - `build\reshade-openxr-ui\simulator-captures\d3d11\save-as.png`
  - `build\reshade-openxr-ui\simulator-captures\d3d11\creating.png`
  - `build\reshade-openxr-ui\simulator-captures\d3d11\created.png`
  - The equivalent `d3d12` captures when the default dual-API workflow is used.
- Inspect every capture with the image viewer. Do not infer visual correctness from file existence, hashes, logs, or a successful probe.
- Confirm title/count/close, Active/Starred/Hidden filters, current preset/navigation/Save As, technique row buttons, footer actions, detail header and parameter controls, keyboard/name field, `Creating...`, and `Created` with the new preset name.
- A passing capture run must exit successfully, report all requested frames rendered, report zero probe failures and zero D3D11/D3D12 validation errors, remove the temporary preset and local-app-data directory, and preserve the pre-run test preset/configuration bytes.

## Fast Iteration Loop

Use this while implementing; each round takes seconds.

1. Design changes (layout, text, color, theme): `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\preview_ui.ps1` compiles only the renderer and writes PNGs of every page and theme to `build\reshade-openxr-ui\preview` (about 5 s, no window, no game). View them with the image viewer, and view them only after the command has finished.
2. Behavior or test changes: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\build.ps1` builds the OpenXR add-on and runs the renderer tests (about 40 s).
3. Occasionally, to see the panel in the headset view: `capture_simulator_ui.ps1 -TestRoot ..\OpenXR-Simulator-test -GraphicsApi D3D11 -Quick` (about 10 s; opens the simulator window). Add `-Theme <name>` for another theme.

Do not run `build.bat`, `smoke.ps1` or a full capture run between small edits. When control positions change, move the click positions in `openxr_ui_renderer_test.cpp` and `capture_simulator_ui.ps1` to the middle of the moved controls.

## Required Validation Order (once, at the end)

1. Run `build.bat`; require the private renderer and central-loader tests to pass.
2. Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\smoke.ps1 -TestRoot <path-to-OpenXR-Simulator-test> -Frames 30`; require D3D11 and D3D12 to complete 30/30 frames with zero failures and zero graphics-validation errors.
3. For layout, text, sizing, or visibility changes, run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\capture_simulator_ui.ps1 -TestRoot <path-to-OpenXR-Simulator-test>` and inspect every screenshot for both APIs.
4. Run editor diagnostics and `git diff --check`.
5. Record material commands and results in the task report. Update `RESHADE-dependency.md` only when the dependency snapshot changes.
