---
description: "Use when building, testing, capturing screenshots, or visually validating the ReShade VR UI Add-on and OpenXR Simulator workflow."
applyTo: "tools/reshade-openxr-ui/**"
---

# ReShade VR UI Add-on Workflow

## Source Of Truth

- Treat the standalone repository source, scripts, and `RESHADE-dependency.md` as the local source of truth.
- Use the existing isolated OpenXR Simulator bundle supplied with `-TestRoot`. Do not rebuild, install, register, or modify it unless the task specifically targets the simulator and the user approves that scope.
- Never deploy to Hubris or launch a game as part of simulator validation.

## Reliable Commands

Run checked-in scripts from the standalone repository root in a clean Windows PowerShell process:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\setup-reshade.ps1 -Action Update
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-openxr-ui.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\verify.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\smoke.ps1 -TestRoot <path-to-OpenXR-Simulator-test> -Frames 30
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\capture_simulator_ui.ps1 -TestRoot <path-to-OpenXR-Simulator-test> -Frames 1600
```

- `clean-build.ps1` removes only the standalone repository's generated `build\` directory before rebuilding.
- If a reused terminal no longer resolves PowerShell commands or changes parsing, start a fresh shell and invoke the checked-in `.ps1` directly.
- In a VS Code task, pass `-NoProfile`, `-ExecutionPolicy`, `Bypass`, `-File`, the script path, and script arguments as separate arguments.
- Use a unique task label when command or arguments differ from an existing task.
- Do not put complex validation logic in nested PowerShell quoting; add or use a checked-in script instead.

## Visual Validation

- Never use debug prints, text-width measurements, hit-test results, or standalone WARP render-target images to decide whether UI text or controls overlap.
- WARP validates interaction commands and state transitions only.
- Visual acceptance requires actual OpenXR Simulator captures produced by `capture_simulator_ui.ps1`:
  - `build\reshade-openxr-ui\simulator-captures\d3d11\main.png`
  - `build\reshade-openxr-ui\simulator-captures\d3d11\detail.png`
  - `build\reshade-openxr-ui\simulator-captures\d3d11\save-as.png`
  - `build\reshade-openxr-ui\simulator-captures\d3d11\creating.png`
  - `build\reshade-openxr-ui\simulator-captures\d3d11\created.png`
  - The equivalent `d3d12` captures when the default dual-API workflow is used.
- Inspect every capture with the image viewer. Do not infer visual correctness from file existence, hashes, logs, or a successful probe.
- Confirm title/count/close, Active/Starred/Hidden filters, current preset/navigation/Save As, technique row buttons, footer actions, detail header and parameter controls, keyboard/name field, `Creating...`, and `Created` with the new preset name.
- A passing capture run must exit successfully, report all requested frames rendered, report zero probe failures and zero D3D11/D3D12 validation errors, remove the temporary preset and local-app-data directory, and preserve the pre-run test preset/configuration bytes.

## Required Validation Order

1. Run `build-openxr-ui.ps1`; require the private renderer and central-loader tests to pass.
2. Run `smoke.ps1 -Frames 30`; require D3D11 and D3D12 to complete 30/30 frames with zero failures and zero graphics-validation errors.
3. For layout, text, sizing, or visibility changes, run `capture_simulator_ui.ps1 -Frames 1600` and inspect every screenshot for both APIs.
4. Run editor diagnostics and `git diff --check`.
5. Record material commands and results in the task report. Update `RESHADE-dependency.md` only when the dependency snapshot changes.
