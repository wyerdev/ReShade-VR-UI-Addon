# ReShade VR UI Add-on

The ReShade VR UI Add-on provides controller-driven headset interfaces for managing ReShade effects in OpenXR and SteamVR/OpenVR applications.

## Completed Features

## Technique list controls

`Active first` is a one-shot button: clicking it moves the currently enabled techniques to the top, updates ReShade's actual processing order, and immediately saves ReShade's active preset. Each technique row also has `^` and `v` controls that move that technique one processing slot and immediately save the resulting order; unavailable top and bottom moves are disabled. ReShade persists the complete order in the preset's root `TechniqueSorting` value, so each game reloads the order from its own selected preset; the add-on does not maintain a separate order file. These preset saves also include any other currently unsaved technique or parameter edits. Later enable or disable actions leave rows where they are. The row's star marks or unmarks that shader as a global favourite, and `Starred only` filters the current game's installed techniques to those favourites. Favourite identities are saved in `%LOCALAPPDATA%\ReShade VR UI Add-on\favourite-shaders.txt`; identities for shaders absent from the current game remain saved but are never displayed until that shader is installed in the active game. Shader-authored techniques marked `hidden`, such as one-frame lookup prepasses, are internal and are not listed. `Show hidden` reveals only techniques hidden by this add-on and changes to `Hide hidden` while that view is active. Each row has a Hide button immediately left of Open; revealed hidden rows show Unhide instead. Hidden identities are saved in `%LOCALAPPDATA%\ReShade VR UI Add-on\hidden-shaders.txt` and entries for shaders absent from the current game are retained. The `X` button at the top right hides the panel; the `Open UI` shortcut shows it again.

## Hide and show UI

The `Open UI` row picks how to hide or show the panel. The controller list offers `L3+R3` (both thumbsticks, the default), `Hold L3+R3`, `B+Y`, `Hold B+Y`, `Hold Menu` (the left controller's menu button) and `Off`; holds need 1.5 seconds and do not repeat until released. The key list offers `Insert` (the default), `End`, `Page Up` and `Off`; the key works only while a window of the game is in front. UEVR uses `L3 + R3` and `Insert` for its own menu, so UEVR users should pick another pair. Controller and key cannot both be `Off`. Both choices are saved as `MenuButton` and `MenuKey` in `settings.ini`. The game still receives every press. The Khronos simple-controller fallback uses its single menu click for every controller choice except `Off`. In SteamVR (OpenVR) the chosen key opens the add-on's dashboard page, which also offers the key choice.

`UI starts open` controls whether a new OpenXR session initially shows the panel. Clicking the checkbox saves the choice immediately in `%LOCALAPPDATA%\ReShade VR UI Add-on\settings.ini`; it takes effect on the next application launch and does not close the current panel. When the UI starts closed, the `Open UI` shortcut still opens it. Existing `%LOCALAPPDATA%\ReShade OpenXR UI`, `%LOCALAPPDATA%\ReShadeOpenXRUI`, or `%LOCALAPPDATA%\OpenXRUIProbe` settings are migrated on first load when possible.

## FPS counter

A small counter above the panel shows the frame rate, frame time, CPU time and a graph of the last 5 seconds. It is always shown while the panel is open; `Always show FPS` (saved as `UI/AlwaysShowFPS` in `settings.ini`) keeps it visible when the panel is closed. Frame times are measured between the game's `xrEndFrame` calls. The CPU line (`CPU 6.2 of 11.1 ms`, where 11.1 ms is the frame budget at the FPS target) is the frame time minus the time spent inside the runtime's `xrWaitFrame` and `xrEndFrame` (two clock reads and a lock-free counter; no extra hook or lock); for SteamVR games it is averaged twice per second from one `GetFrameTimings` batch as the client frame interval minus the `WaitGetPoses` wait. The panel header and parameter footer show the headset's recommended per-eye resolution next to DX11/DX12 (`xrEnumerateViewConfigurationViews` once at session creation); the SteamVR page shows `GetRecommendedRenderTargetSize`, asked about every five seconds, next to the CPU line. The frame rate covers the last half second. In the graph each bar is a frame time, newest on the right, with a faint line for every second. Horizontal lines mark one, two and three headset refreshes (11, 22 and 33 ms at 90 Hz): green bars are on time, yellow missed one refresh, red missed more. The refresh rate comes from the headset: OpenXR games use the runtime's predicted display period from `xrWaitFrame`, SteamVR games ask SteamVR for the headset display frequency about every five seconds. Until a rate is reported the graph assumes 90 Hz and the label shows `90 Hz?`. `FPS target` (panel drop-down, radio buttons on the SteamVR page; saved as `UI/FrameRateDivisor`) chooses full, half or third rate; at half rate on a 90 Hz headset 45 FPS is green and the label shows `45 of 90 Hz`. A stutter is drawn as wide as it lasted and scrolls off after 5 seconds. Gaps over one second (loading, headset removed) are left empty. The counter image is redrawn twice per second. In SteamVR games the same line and graph appear at the top of the ReShade dashboard page.

## Performance mode

`Performance mode (on = faster shaders)` reads and writes ReShade's own `GENERAL/PerformanceMode` value for the active OpenXR effect runtime. The choice is saved immediately to `ReShadeVR.ini` through the official add-on configuration API and takes effect on the next application launch. ReShade then compiles preset values as constants for a more optimized effect representation.

Performance mode intentionally removes editable uniform variables. Technique enable and disable controls remain available, but shader parameter controls are unavailable until performance mode is unchecked and the application is restarted again. ReShade bakes the values into the shaders as constants and offers add-ons no way to read them, so while the game runs in performance mode an opened effect shows a yellow note explaining this instead of its value rows.

Performance mode and `Depth buffer (off = faster in OpenGL)` sit together because ReShade reads both only at game start. The line below them reads "These two apply after a game restart." and turns amber ("Restart the game to apply your change.") while a saved value differs from the one the game started with.

## Temporary shader disable and saving

`Disable all` turns every technique off without saving the preset. It captures the exact live technique state first, including changes that have not been saved; `Restore shaders` restores that captured state. Technique editing and Save are unavailable while the temporary all-off state is active, preventing it from being written to the preset.

Save is green when the current live technique and parameter state matches the last successful preset save. It turns amber after either a technique toggle or a parameter change/reset. A successful Save briefly changes the label to `Saved`; a failed save is red. Parameter changes remain live but unsaved until Save is pressed. Values the game itself keeps writing (for example a VR mod driving fog settings every frame) are ignored: a value written by something other than the panel in 60 or more frames while ReShade's own menu is closed counts as set by the game and no longer makes Save amber. Uniforms with a `source` annotation (timer, frame count and similar) are not listed.

## Per-shader revert and restore

Each technique detail view has one toggle control labeled `Revert` or `Restore`. `Revert` is disabled when the technique's live enabled state and parameter values match the last successful preset save. When enabled, it captures the current live state and applies the saved values; the same control then becomes `Restore` and puts the captured live state back. The temporary state is cleared when the active preset or effect inventory changes; neither action saves the preset automatically.

## Cursor stabilization

Free pointer movement uses a six-pixel radial dead zone followed by continuous low-pass motion and two-pixel output quantization. This avoids converting gradual controller drift into delayed cursor jumps. Holding select uses a faster low-pass response instead of raw pose coordinates, keeping sliders and drag controls responsive without exposing every tracking sample.

## Effects in subfolders

ReShade searches each `EffectSearchPaths` folder only at the top level unless the entry ends in `\**`. Some setups (for example the D2R-PCVR mod: `.\reshade-shaders\Shaders`) therefore skip effect packs in subfolders such as `SweetFX`. Once per effect runtime, after its first inventory, the add-on reads the list through the add-on configuration API, resolves relative entries from ReShade's base folder (`ReShadeGetBasePath`), and walks each non-recursive folder (at most 20,000 entries; entries with `${...}` macros are skipped). If `.fx` or `.addonfx` files sit in subfolders ReShade does not search, the panel shows a notice under the header, for example "42 effects in 8 subfolders are not loaded.", with `Load subfolders` and `Not now`; hovering the text explains it.

`Load subfolders` is disabled only while `Disable all` is active. A reload re-reads the preset from disk, so with unsaved changes the notice warns that they will be dropped and the button reads `Load anyway` (amber). It appends `\**` to the affected entries, saves them with `set_config_value` (verified by reading back) and asks ReShade to reload all effects on its own thread. While ReShade rebuilds every effect, all effects are off and the list is empty; the notice shows "Loading effects... N s. Effects are off until it finishes." with a seconds counter and a sliding bar, and the empty list says "ReShade is loading effects" instead of "No ReShade techniques found". In D2R this took roughly 10 to 30 seconds the first time; the add-on log records the duration. The notice finally shows either "Effects from subfolders loaded. No restart needed." (a reloaded technique comes from one of the found files) or "Subfolders saved. Restart the game to load their effects." (the reload found none, or did not start within 900 frames). If saving fails it says "Could not change ReShade.ini." and the hover text explains the manual fix. `OK` or `Not now` hide the notice until the next game start. The SteamVR page shows the same notice with plain buttons when its hidden controls are switched back on (see the SteamVR frontend note below).

## TODO Features:

## Auto sort
more text to come

## Guestures
Move left or right hand close to head and press
Activate/Deactivates a set of shaders


## Architecture

The package shares one ReShade state core across two frontend DLLs:

- A ReShade add-on core observes the windowless OpenXR or OpenVR effect runtime and publishes an immutable, thread-safe view-model snapshot. Effect reloads rebuild structural metadata; ReShade technique, uniform, and ordering events invalidate live state; and each present reconciles technique order, enabled states, and parameter values from the runtime's public getters. ReShade remains the single source of truth regardless of whether a change came from this UI, preset loading, another add-on, or another ReShade control surface.
- `ReShade-OpenXR-UI.addon64` is both the core and an OpenXR API layer. It creates a D3D11, D3D12 or OpenGL swapchain and submits it as an `XrCompositionLayerQuad` in VIEW space.
- A private Dear ImGui context renders a vertically scrollable technique list into that swapchain with the matching vendored D3D11, D3D12 or OpenGL 3 backend. It does not use ReShade's `register_overlay` path or context.
- The panel never makes the game wait on the GPU: no GPU waits, fence waits or flushes per frame. D3D12 uses a ring of four command allocators and skips a menu frame if the next one is still in use; at session end busy D3D12 objects are freed later instead of waited for.
- The add-on never changes effect values inside a ReShade event callback. ReShade's built-in desktop/headset Effect Runtime Sync only stops its own echo when a change starts outside another add-on's callback; a reset inside `reshade_reloaded_effects` bounced between the two runtimes until the stack overflowed. Default values are therefore captured from the menu path (`openxr_ui_get_techniques`), 32 uniforms per call.
- While a windowless headset runtime exists, effects on windowed (monitor) runtimes are switched off with `set_effects_state(false)`, which fires no sync event, and switched back on when the last headset runtime is destroyed.
- `ReShade-SteamVR-UI.addon64` compiles the same core with `openvr_ui_overlay.cpp` and no private ImGui implementation. It registers a settings callback through ReShade's exported ImGui function table, while ReShade's existing SteamVR dashboard handles texture submission and input. The controls appear by expanding **ReShade VR UI Add-on for SteamVR** on the **Add-ons** page, avoiding ReShade's fixed 500-pixel VR tab row entirely. The page currently shows only the FPS line, graph and FPS target: everything below them (presets, effect list, Performance mode, Depth buffer, the subfolder notice) duplicates ReShade's own menu, so it is hidden by `show_effect_controls = false` in `openvr_ui_overlay.cpp`. Set it to `true` to show those controls again; the code still compiles either way.
- The optional System-Wide SteamVR Listener is a separate program in `tools\steamvr-listener` with its own build (`build-listener.bat`, also run by `build.bat`), package and installer. It is not part of this add-on's package or install.

In an OpenVR-only process, the companion writes `ADDON/SyncEffectRuntimes=0` before ReShade's built-in Effect Runtime Sync receives its first runtime initialization event. ReShade's dashboard operates on the OpenVR runtime directly. This prevents desktop-to-headset preset and uniform feedback while leaving native OpenXR behavior unchanged. When the windowless OpenVR runtime first appears, the companion also checks `ADDON/SteamVROverlayScaleNormalized`. If the marker is absent and the runtime still has ReShade's default font size and an automatic `1`, `1.5`, or `2` scale, it writes `STYLE/FontSize=12` and `STYLE/FontScale=1` to that runtime's `ReShadeVR.ini` before recording the marker. Custom sizing and subsequent user changes are preserved. Empty asynchronous inventories are retried for up to 600 presents. Once the inventory is non-empty, the companion calls `IVROverlay::ShowDashboard("reshade")` once so an already-running SteamVR dashboard surfaces the process-created ReShade overlay. ReShade persists collapsed add-on entries in `ADDON/OverlayCollapsed`; users expand the companion entry to invoke its settings callback.

The right-controller aim pose drives ImGui hit testing and draws a crosshair at the panel intersection. Hold the bound trigger/select action on the main vertical scroll control and move the controller to browse techniques without pages. Once a drag starts, it remains the exclusive interaction until three consecutive rendered trigger-up frames; pointer drift outside the control, including rightward motion over Open, cannot activate another control during the hold. Vertical movement is scaled to 90% for finer scrolling while retaining full-list travel. Right-thumbstick up/down also scrolls the technique list: deflection beyond a 50% dead zone moves one row immediately and repeats every eight rendered frames while held. Thumbstick scrolling yields to trigger drag capture, and thumbstick click continues to show or hide the panel. The vector binding is available on the Oculus Touch interaction profile; the simple-controller fallback has no thumbstick vector component.

Each technique row retains its green/gray tick box and has a separate Open button at the right. Clicking anywhere else in the row, including the label and tick box, enables or disables the technique; Open only enters its detail view. The detail view exposes every user-editable uniform component from its effect in one continuous list, with the same vertical scrollbar and right-thumbstick row scrolling as the main technique list. Each parameter uses a two-tier row with its label above the slider, value, and buttons, so the controls no longer consume the label width. Labels that still exceed the panel are abbreviated with an ellipsis and reveal their complete text on hover. ReShade's standard `ui_tooltip` annotation is shown in the same wrapped hover panel when present. Boolean values use a toggle. Numeric values use wide draggable sliders for large changes, annotation-derived minus/plus controls for fine tuning, and `-10x`/`+10x` controls for coarse steps. Each value has a compact circular-arrow reset button that is only active when the value differs from its default; its tooltip shows the exact default, and sliders mark the default position with a thin tick. Reset All restores every resettable value in the effect. Parameter changes and resets mark the preset dirty until `Save` calls ReShade's current-preset save operation. The button then shows a green `Saved` acknowledgement or a red failure state from the actual call result. Back returns to the technique list.

Technique order remains stable until `Active first` performs one stable grouping of the currently enabled techniques or a row's `^`/`v` control swaps one adjacent processing slot. Both paths pass the complete handle sequence to ReShade's public `effect_runtime::reorder_techniques` API and call `save_current_preset`; ReShade then renders in that order and persists it as `TechniqueSorting` in the active preset. Ordering is blocked during temporary shader disable so that transient state cannot be saved. ReShade inventory refreshes preserve that user-established identity order and append only newly discovered techniques. Hide/Unhide and favourite stars use globally persisted raw effect-file and technique identities without discarding identities absent from the current game. Both filters operate only on ReShade's current technique inventory, so unavailable shaders are never rendered as hidden or favourite rows. Technique names and descriptions consume the shader's standard `ui_label` and `ui_tooltip` annotations when present. The main list keeps the description in its hover tooltip, while the selected shader's complete description is persistently wrapped below the detail header without a fixed-height clip. Parameter labels are painted after their row controls so slider and button backgrounds cannot cover them. Parameter labels and displayed values turn amber when their current component differs from the captured shader default, with floating-point noise ignored. The corresponding technique label on the main page also turns amber when its effect contains any non-default value.

The main header uses three explicit rows so no text shares space with another control: title/count with `Pin UI`, `Expand`/`Collapse` and close at the right, `Active first`/`Starred only`/`Show hidden`, then current preset name/navigation/`Save`/`Save As`/`Delete`. The arrow controls use ReShade's public current-preset path API to cycle through valid `.ini` and `.txt` presets beside the active preset, including wraparound. ReShade saves the outgoing preset before each switch. Preset controls do not refresh or mutate UI state directly: the same runtime projection used for every ReShade state change updates tick boxes, technique order, parameter values, default-value highlighting, and dirty feedback on the next rendered frame. `Save` persists the current preset and reports its result. `Save As` opens a dedicated headset naming screen with a visible name field, controller-clickable keyboard, case toggle, Space, Backspace, Clear, Cancel, and Create. Invalid Windows filenames and existing sibling presets are rejected. Create asks ReShade to save the current state, waits for ReShade's normal present-path INI flush, copies that ReShade-generated preset to the requested sibling name, and switches to it through the same public API. Its button reports `Creating...`, then `Created` or `Failed`; successful creation also refreshes the displayed current preset name. A dirty preset must show an updated file timestamp within three seconds or Save As cancels without creating a stale copy. Each successful Save As therefore creates a separate preset file; the add-on does not introduce another preset format or parser. `Delete` switches to a valid sibling, waits for ReShade to flush the outgoing preset's cached state, and only then removes the former current file so that ReShade cannot recreate it. It is inert when no sibling can be selected.

Expand doubles the panel height and visible entries from 10 to 20 techniques or from 5 to 10 parameter components. Collapse restores the compact panel. The detail view repeats `Pin UI` and `Expand`/`Collapse` at the right of its footer.

The `Open UI` controller and key choices hide or reopen the panel (the simple-controller fallback uses its menu click). The main footer uses four rows. Left: `Always show FPS`, `UI starts open`, the `Open UI` row with its controller and key lists, then temporary disable. Right: the FPS target, `Performance mode (on = faster shaders)`, `Depth buffer (off = faster in OpenGL)`, then the restart note for those two. `Pin UI` (session only) keeps the panel and FPS counter at a fixed world direction (yaw and pitch, no roll) at their usual distance around the head, submitted in LOCAL space. The pin survives closing the panel, so an always-shown FPS counter stays in place and the panel reopens where it was pinned. Hidden frames do not submit the quad. Free hover uses a six-pixel radial dead zone, 18% follow rate, and two-pixel output quantization; held select uses a responsive 35% low-pass. Each compact technique row keeps the technique and effect names on one line to avoid vertical clipping. Uniforms marked `hidden` or `noedit` are not exposed, and `noreset` values do not offer reset controls.

The source is split by responsibility: `openxr_ui_layer.cpp` owns OpenXR session, input, and D3D11/D3D12/OpenGL swapchain flow; `reshade_openxr_ui_addon.cpp` owns ReShade callbacks, runtime handles, and technique state; `openxr_ui_preset.cpp` owns preset dirty-state comparison, sibling navigation, saving, pending inherited-copy, and pending deletion; and `openxr_ui_renderer.cpp` owns the private ImGui context and its D3D11, D3D12 and OpenGL rendering backends. Preset code is deliberately represented by bounded operation-specific types rather than a generic manager object.

## Requirement

Use the official ReShade build with full add-on support. The standard official build has limited add-on support and deliberately skips standalone `.addon64` files.

The original `C:\ProgramData\ReShade\ReShade64.dll` version 6.8.0.2158 was the limited build and could not run this add-on. The user replaced it through the official ReShade setup with the full add-on-support build; a custom ReShade build is not required.

## Build

From the standalone repository root, the standard build command is:

```bat
build.bat
```

For a clean rebuild and package:

```bat
clean.bat
```

`build.bat` sets up the local ReShade dependency when needed, builds the
add-ons and the separate SteamVR listener, and creates both release packages.
`build-addon.bat` builds and packages only the add-ons. `clean.bat` runs
`clean-addon.bat` and `clean-listener.bat`. `clean-addon.bat` removes only
`build\reshade-openxr-ui`, including simulator captures stored there, then runs
the normal build, package step, and enforced D3D11 WARP interaction test. The
clean command assumes the ReShade dependency checkout already exists.

For direct PowerShell control, use `build-openxr-ui.ps1` or
`clean-build.ps1`; those wrappers build the add-ons but leave packaging to
`package.ps1`.

Output:

```text
build\reshade-openxr-ui\ReShade-OpenXR-UI.addon64
build\reshade-openxr-ui\ReShade-SteamVR-UI.addon64
build\reshade-openxr-ui\ReShade-OpenXR-UI-Renderer-Test.exe
build\reshade-openxr-ui\layers\ReShade-OpenXR-UI-Layer.json
```

The manifest expects the OpenXR DLL one directory above its `layers` directory. Its `api_layer.name` value, `XR_APILAYER_reshade_vr_ui`, is a stable external identifier used by SteamVR's API-layer toggle and must not be renamed; `build.ps1` enforces it. The user-facing label belongs in `api_layer.description`. The SteamVR DLL has no OpenXR negotiation export. The OpenXR DLL is installed centrally; the SteamVR DLL is copied by the user into each SteamVR game folder.
To test a SteamVR game with the local build, double-click `install_steamvr_addon.bat` in the repository root (default game: Propagation: Paradise Hotel). Run it with `uninstall` to remove the add-on or `status` to check; a second argument picks another game's `.exe`. It only copies or deletes the add-on file; ReShade itself must be installed for the game with ReShade Setup first. `status` also reports settings that an earlier add-on run left in the game's `ReShade.ini` and `ReShadeVR.ini`; they keep ReShade's icon appearing in SteamVR and its menu readable even after the add-on is removed, so delete them to test plain ReShade (see `INSTALL-SteamVR.md`).

The build executes the D3D11 WARP interaction test after compiling. It fails if the test fails.

Verify the artifact size, SHA-256, and required exports with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\verify.ps1
```

## Simulator Smoke

`test.bat` in the repository root runs `build.bat`, the smoke test and the visual capture below in that order, then prints one PASSED or FAILED line per step and exits with 0 only if all passed. Pass the simulator bundle as its first argument, or set `RESHADE_VR_UI_TEST_ROOT`.

The smoke test stages both the add-on and a uniquely named local ReShade layer in the isolated simulator bundle. It disables the machine-wide ReShade layer only for the probe process, restores all environment variables, and removes its staged files afterward.

It also runs a D3D11 WARP test with a 12-technique and eight-parameter snapshot before the OpenXR probe. That test verifies preset previous/next/Save/Save As/Delete commands, global favourite star commands and favourites-only filtering, exclusive drag capture while the pointer crosses an Open button, sustained release ownership, slower vertical technique dragging, right-thumbstick scrolling in both directions, separate technique checkbox and Open controls, startup-visibility and next-start performance-mode commands, temporary disable/restore command generation, Save and Delete blocking while temporarily disabled, detail navigation, held numeric slider movement, fine and coarse increments, boolean toggles, per-value reset, reset-all, shader-parameter joystick scrolling and held vertical-scrollbar movement, back navigation, and expanded-row selection. The OpenXR leg requires a newly written ReShade log containing add-on-specific ImGui initialization, technique-model, and quad-submission evidence.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\smoke.ps1 -Frames 30
```

The visual capture script runs both the D3D11 and D3D12 probes and drives the real OpenXR action path through the simulator's controller command interface. By default each probe gets 4000 validation frames plus a 1000-frame interaction and cleanup reserve (`-Frames` changes the first number); the probe keeps rendering while each screenshot is taken, so a smaller budget can run out before the last step. Every wait for a frame stops after 30 seconds, and OpenXR add-ons installed on the PC (including an installed copy of this add-on) are switched off for the test process only, through each add-on's own off switch. Each API captures preset Off, On, and Off-again states in one session, then captures main, the open FPS target list (`fps-target-open.png`), detail, Save As naming, `Creating...`, and `Created` states. It types `HI` in Save As, retries an actual simulator-window capture if `PrintWindow` returns a blank D3D12 client image, uses process-scoped simulator runtime and layers, restores the managed test preset/configuration byte-for-byte, and removes its temporary add-on, manifests, local app data, and created `HI.ini` afterward.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\capture_simulator_ui.ps1 -TestRoot C:\path\to\OpenXR-Simulator-test
```

Captures are written under `build\reshade-openxr-ui\simulator-captures\d3d11` and `build\reshade-openxr-ui\simulator-captures\d3d12`. Pass `-GraphicsApi D3D11` or `-GraphicsApi D3D12` to either simulator script for a focused run; the default is `Both`.

## Global Installation

"Global" means one protected machine-wide OpenXR runtime installation with per-user preferences. Installation requires administrator access so SteamVR's elevated OpenXR API-layer control can change the layer state. It does not replace, uninstall, or modify the official ReShade files under `C:\ProgramData\ReShade`, and it does not copy files into individual game directories.

Admin is also required for games that run as administrator, such as SOMA with the SOMA-VR mod (`Launch-SOMA-VR.cmd` always restarts itself and the game as administrator). The OpenXR loader ignores per-user (`HKCU`) layer registrations in elevated processes (`manifest_file.cpp`, `IsHighIntegrityLevel`), so only the `HKLM` registration loads there. A no-admin per-user install (`%LOCALAPPDATA%` plus `HKCU`) was tried again on 2026-10-23. It worked in Resident Evil 9, Paradise Hotel and Gothic Remake (UEVR), but not in SOMA-VR, so it was reverted. Do not reintroduce it without solving elevated games.

The supported developer flow starts from the repository root and creates the
same layout shipped to users:

```bat
build.bat
install.bat
```

`package.ps1` creates `build\reshade-openxr-ui-release\ReShade-VR-UI-Addon-OpenXR.zip` and `ReShade-VR-UI-Addon-SteamVR.zip`, each with an unpacked directory of the same name. Release builds add the release number and date to the ZIP names only (for example `-v12-2026-10-24`, from `RESHADE_VR_UI_RELEASE_SUFFIX`). `Install-OpenXR.cmd` must run from the complete OpenXR layout because it expects the DLL beside the script and the manifest under `openxr-layers`.

Installation copies exactly these runtime files:

```text
%ProgramFiles%\ReShade VR UI Add-on\ReShade-OpenXR-UI.addon64
%ProgramFiles%\ReShade VR UI Add-on\openxr-layers\ReShade-OpenXR-UI-Layer.json
```

It then creates one enabled `REG_DWORD` value with data `0`. The value name is the absolute installed manifest path:

```text
Key:   HKLM\Software\Khronos\OpenXR\1\ApiLayers\Implicit
Value: %ProgramFiles%\ReShade VR UI Add-on\openxr-layers\ReShade-OpenXR-UI-Layer.json
Data:  0
```

The manifest resolves `..\ReShade-OpenXR-UI.addon64` relative to its own `openxr-layers` directory. The OpenXR loader can therefore discover it in every 64-bit OpenXR process for this user.

Official ReShade still controls which games receive ReShade effects and OpenXR support. Install this add-on once, but continue using the official ReShade installer to enable ReShade and OpenXR for each game. The official **full add-on support** build is required; the standard limited build rejects standalone add-ons. The current UI renderer supports 64-bit D3D11, D3D12 and OpenGL OpenXR sessions.

Close games and VR runtimes before installing or updating so they cannot retain previous files. Run `Install-OpenXR.cmd` as administrator. Re-running it updates all installed runtime files and preserves the OpenXR layer's On or Off value. Verify the installation from an elevated PowerShell window with:

```powershell
Get-Item -LiteralPath "$env:ProgramFiles\ReShade VR UI Add-on\ReShade-OpenXR-UI.addon64"
Get-Item -LiteralPath "$env:ProgramFiles\ReShade VR UI Add-on\openxr-layers\ReShade-OpenXR-UI-Layer.json"
reg.exe query "HKLM\Software\Khronos\OpenXR\1\ApiLayers\Implicit" /v "$env:ProgramFiles\ReShade VR UI Add-on\openxr-layers\ReShade-OpenXR-UI-Layer.json" /reg:64
```

The registry query must report `REG_DWORD    0x0`. This registration is independent of official ReShade's machine-wide OpenXR registration; both are required for the add-on to activate in a ReShade-enabled game.

To remove only this add-on from the repository installation, run
`uninstall.bat` as administrator. For an extracted release, run `Uninstall-OpenXR.cmd`
from the release directory as administrator. Both paths remove and verify the
HKLM layer registration and remove the OpenXR DLL and the manifest.
They do not change official ReShade, game files, effects, presets, ReShade
configuration, or logs. They intentionally preserve preferences.

## Headset Test

Last headset check (2026-09-26): Hubris (native OpenXR) and UEVR games on a Pimax headset; the FPS counter and graph work.

Only the user launches Hubris. Keep SteamVR stopped. The central installation should activate automatically because official ReShade OpenXR is already enabled for Hubris; no Hubris-local add-on DLL, manifest, or registry value is needed. Confirm the UI appears in Hubris but not in Pimax Home, which has no ReShade add-on API to satisfy the activation gate.

Verify that pressing both thumbsticks (`L3 + R3`) and the `Insert` key toggle the panel, then pick `Hold B+Y` and verify a short press does nothing while a 1.5-second hold toggles it. Check that free-hover jitter is reduced and a held numeric slider still follows small controller movements. Drag the main vertical scroll control in both directions, and move the held pointer over technique rows to confirm none open before release. Open an enabled technique and verify its real shader parameters are readable. Try the fine and `10x` controls, scroll the parameter list in both directions with the right thumbstick and vertical scrollbar, expand and collapse the panel, reset individual values, use Reset All, press Save, and return with Back. Exit and relaunch once to confirm saved technique and parameter changes persist.

Expected evidence:

ReShade VR UI Add-on
```text
ReShade VR UI Add-on: dual-role OpenXR API-layer boundary initialized.
ReShade VR UI Add-on: native VIEW-space quad initialized with private ImGui technique renderer.
ReShade VR UI Add-on: native quad graphics backend: D3D11.
ReShade VR UI Add-on: right-controller aim and select actions attached.
ReShade VR UI Add-on: submitting native XrCompositionLayerQuad in VIEW space.
ReShade VR UI Add-on: observed windowless OpenXR effect runtime ...
ReShade VR UI Add-on: enumerated ... techniques (... enabled) and ... uniforms.
ReShade VR UI Add-on: scrollable technique model ready for controller input.
ReShade VR UI Add-on: controller enabled <effect>::<technique>.
```

The current renderer supports D3D11, D3D12 and OpenGL OpenXR sessions. The backend evidence line reports `D3D11`, `D3D12` or `OpenGL` for the active session. The quad stays visible while the targeted application is running for this static interaction milestone.

## Next Development Phase

1. Headset-validate technique descriptions, enabled-first ordering, amber non-default parameter values, and the existing controls.
2. Confirm edited technique and parameter values persist after a user-initiated Hubris restart.
3. Add richer widgets for combo lists and color values after the compact and expanded controls are validated.