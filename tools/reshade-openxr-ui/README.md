# ReShade VR UI Add-on

ReShade VR UI Add-on is a controller-driven, headset-native interface for managing ReShade effects in OpenXR and SteamVR/OpenVR applications. The package has two frontends over the same ReShade state model: an OpenXR API-layer quad and a lightweight ReShade add-on page in the existing SteamVR dashboard.

This README is for developers. See [INSTALL.md](INSTALL.md) to install a prebuilt release.

## Install And Load Model

The protected runtime files are installed machine-wide under:

```text
%ProgramFiles%\ReShade VR UI Add-on\
├── ReShade-OpenXR-UI.addon64
├── ReShade-SteamVR-UI.addon64
├── ReShade-VR-Loader.exe
└── openxr-layers\
	└── ReShade-OpenXR-UI-Layer.json
```

Preferences and logs remain per-user and are preserved by the uninstaller:

```text
%LOCALAPPDATA%\ReShade VR UI Add-on\settings.ini
%LOCALAPPDATA%\ReShade VR UI Add-on\hidden-shaders.txt
%LOCALAPPDATA%\ReShade VR UI Add-on\favourite-shaders.txt
%LOCALAPPDATA%\ReShade VR UI Add-on\ReShade-VR-Loader.log
```

`Install.cmd` creates this enabled machine-wide OpenXR API-layer registration:

```text
Key:
HKEY_LOCAL_MACHINE\Software\Khronos\OpenXR\1\ApiLayers\Implicit

Value name:
%ProgramFiles%\ReShade VR UI Add-on\openxr-layers\ReShade-OpenXR-UI-Layer.json

Type:
REG_DWORD

Data:
0
```

The manifest's internal `api_layer.name`, `XR_APILAYER_reshade_vr_ui`, is a stable SteamVR-facing identifier and must not be renamed. SteamVR displays `api_layer.description` but passes `api_layer.name` to its enable/disable helper; changing the identifier can leave the visible toggle unable to update the layer state. The normal build rejects a renamed identifier.

Data `0` means the layer is enabled. When a 64-bit OpenXR application starts:

1. The Windows OpenXR loader reads the machine-wide implicit API-layer registration.
2. The loader reads `ReShade-OpenXR-UI-Layer.json`.
3. The manifest resolves `..\ReShade-OpenXR-UI.addon64` relative to its `openxr-layers` directory.
4. The loader loads the DLL as an OpenXR API layer.
5. The DLL checks whether official ReShade with full add-on support is loaded.
6. If ReShade accepts `reshade::register_addon`, the add-on initializes its controller actions, D3D11 or D3D12 UI swapchain, and headset quad.
7. Without ReShade, the layer remains inert and forwards OpenXR normally. Pimax Home therefore does not display the UI.

`Install.cmd` also creates a current-user startup entry for `ReShade-VR-Loader.exe`. The loader reads `C:\ProgramData\ReShade\ReShadeApps.ini`, which official ReShade Setup maintains when OpenXR is enabled, and waits for each allowlisted 64-bit process to select its VR API. If `openxr_loader.dll` appears, the loader leaves activation entirely to the registered OpenXR layers. If `openvr_api.dll` appears, it loads the exact central `ReShade64.dll` and then `ReShade-SteamVR-UI.addon64`. This requires no game-local proxy or add-on and prevents the SteamVR path from creating an extra desktop runtime in a native OpenXR launch.

The DLL has two simultaneous roles:

- **OpenXR API layer:** creates and submits the headset-visible UI.
- **ReShade add-on:** reads and changes techniques, parameters, ordering, and presets through ReShade's public API.

Official ReShade remains separate under `C:\ProgramData\ReShade`. Nothing is copied into Hubris or other game directories. The loader deliberately reuses official ReShade's central application allowlist rather than maintaining another list. Install this package once as administrator and it becomes available to compatible 64-bit D3D11 and D3D12 OpenXR or OpenVR games enabled through official ReShade.

Run the packaged installer as administrator from the repository root after closing running OpenXR applications:

```powershell
.\build\reshade-openxr-ui-release\ReShade-VR-UI-Addon\Install.cmd
```

## Compatibility

- Windows x64
- 64-bit OpenXR or OpenVR applications
- Direct3D 11 or Direct3D 12 renderer
- Unreal games run through UEVR in OpenXR mode (the add-on creates its own effect runtime; no depth effects)
- Oculus Touch-compatible controllers
- Khronos simple-controller profile as a limited fallback

Vulkan and OpenGL application sessions are not currently supported. SteamVR/OpenVR is supported for 64-bit D3D11 and D3D12 games through the packaged `ReShade-SteamVR-UI.addon64` companion.

SteamVR itself is not the compatibility boundary. The OpenXR frontend works when an application uses OpenXR and SteamVR is the active OpenXR runtime. An OpenVR application never loads the OpenXR API-layer chain, so the central loader recognizes `openvr_api.dll`, starts ReShade, and loads the companion. ReShade's existing SteamVR dashboard owns compositor texture submission and controller-to-mouse input; the companion registers its focused controls as the settings panel of **ReShade VR UI Add-on for SteamVR** on ReShade's **Add-ons** page. This avoids clipping in ReShade's fixed-width VR tab row. Once a non-empty VR effect inventory is available, the companion asks SteamVR to show the existing `reshade` dashboard overlay so launches made while SteamVR is already running do not depend on dashboard tile refresh.

The companion disables ReShade's built-in Effect Runtime Sync for OpenVR-only processes before its first runtime initializes. The dashboard callback already receives the headset runtime directly, while synchronizing its changes through the simultaneous desktop runtime can recursively re-enter preset and uniform propagation. This setting is written to the enabled game's `ReShade.ini`. Native OpenXR processes retain their existing synchronization behavior.

The companion also performs a one-time density migration for ReShade's fixed 500-pixel SteamVR dashboard. When the OpenVR effect runtime still has the default font size and an automatic `1`, `1.5`, or `2` scale, it writes `STYLE/FontSize=12` and `STYLE/FontScale=1` to `ReShadeVR.ini`. A per-runtime marker prevents later user-selected font settings from being overwritten. The portable VR configuration generator uses the same baseline.

For a Vulkan OpenXR application, the application supplies an `XrGraphicsBindingVulkanKHR` and Vulkan swapchain images. The current add-on accepts `XrGraphicsBindingD3D11KHR` and `XrGraphicsBindingD3D12KHR`, owns matching graphics resources, and renders Dear ImGui through the corresponding backend. It therefore leaves a Vulkan session running but skips native-quad initialization and logs `native quad skipped: the OpenXR session is not using D3D11 or D3D12.` SteamVR acting as the OpenXR runtime does not translate Vulkan images into a Direct3D graphics binding for API layers, so changing the runtime to SteamVR does not make the UI renderer compatible. Vulkan UI support still requires a Vulkan swapchain-image path and Vulkan ImGui renderer in this add-on.
## Features

- Technique enable/disable, ordering, favourites, and hiding
- Shader parameter editing with sliders, steps, defaults, and reset actions
- Per-shader Revert to the last successful preset save and Restore to the captured live state
- Preset navigation, save, named Save As, and delete
- Controller pointer, trigger interaction, thumbstick scrolling, and `L3 + R3` visibility control
- Active graphics API indicator for Direct3D 11 and Direct3D 12 sessions
- Distinct diagnostics for a missing OpenXR effect runtime and an empty technique inventory
- Compact and expanded panel layouts
- Temporary disable and restore for all techniques
- FPS counter with CPU time, a 5-second frame-time graph and a full/half/third-rate FPS target
- Headset per-eye resolution shown next to the graphics API
- Persistent UI preferences under `%LOCALAPPDATA%\ReShade VR UI Add-on`

The project has these main implementation boundaries:

- `openxr_ui_layer.cpp` owns OpenXR negotiation, sessions, controller actions, D3D11/D3D12 swapchain resources, and quad submission.
- `openvr_ui_overlay.cpp` owns the focused page rendered by ReShade's existing SteamVR dashboard.
- `steamvr_loader.cpp` watches official ReShade's central allowlist and loads the central ReShade and SteamVR UI modules into matching processes.
- `reshade_openxr_ui_addon.cpp` owns ReShade callbacks, runtime handles, and technique and uniform adaptation.
- `openxr_ui_preset.cpp` owns preset state comparison, navigation, saving, copying, and deletion.
- `openxr_ui_renderer.cpp` owns the private Dear ImGui context, interaction model, and D3D11/D3D12 rendering backends.

ReShade is the source of truth for live technique order, enabled state, parameters, and the current preset. The add-on publishes immutable snapshots for the renderer and reconciles them from ReShade's public API after relevant events and presents.

The renderer also receives whether the add-on has observed a windowless ReShade VR effect runtime. It reports that state separately from an initialized runtime with an empty technique inventory. The former is a ReShade graphics-initialization failure; only the latter points toward shader search paths or effect installation.

The OpenXR manifest and DLL are installed once in a protected machine-wide location. The layer attempts to register the same DLL through ReShade's full add-on API after the OpenXR layer chain is created. Controller actions, the swapchain, and the headset UI are initialized only when that registration succeeds. Compatible ReShade OpenXR games therefore activate automatically, while unrelated OpenXR applications remain unaffected. No per-game add-on allowlist is maintained.

## Requirements

- Git
- Windows PowerShell 5.1 or newer
- Visual Studio 2022 or Visual Studio 2022 Build Tools
- The **Desktop development with C++** workload, including MSVC x64 and a Windows SDK
- A ReShade source checkout with the Dear ImGui, OpenXR, and OpenVR submodules

CMake is not required. The build script locates Visual Studio with `vswhere.exe` and invokes MSVC directly.

## Source Layout

This standalone repository keeps the plugin under `tools\reshade-openxr-ui`.
ReShade is fetched separately into the ignored `.deps\reshade` checkout. Its
current source snapshot is recorded in the repository root's
`RESHADE-dependency.md`; it is informational and is not a version pin.

Set up the latest ReShade checkout from the standalone repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\setup-reshade.ps1 -Action Setup
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\setup-reshade.ps1 -Action Update
```

`Setup` clones the latest upstream default branch when the checkout is absent.
`Update` fetches and fast-forwards that checkout later. Either command
initializes the required submodules. An existing checkout can be supplied with
`-ReShadePath` instead of using `.deps\reshade`.

Updating ReShade may change `RESHADE_API_VERSION`. Treat that as a compatibility change and run the complete validation workflow before publishing a release against the newer revision.

## Build

Run the standard build and package flow from the repository root:

```bat
build.bat
```

For a clean rebuild and package:

```bat
clean.bat
```

`build.bat` sets up the local ReShade dependency when needed. `clean.bat`
assumes the dependency checkout already exists and removes the generated build
output before rebuilding. Both commands package the validated build.

For direct PowerShell control or a separately cloned ReShade checkout:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-openxr-ui.ps1 -ReShadePath C:\path\to\reshade
```

The PowerShell wrapper builds the add-ons but does not package them; run
`tools\reshade-openxr-ui\package.ps1` separately when needed. The build runs
the D3D11 WARP interaction suite. Outputs are written to:

```text
build\reshade-openxr-ui\ReShade-OpenXR-UI.addon64
build\reshade-openxr-ui\ReShade-SteamVR-UI.addon64
build\reshade-openxr-ui\ReShade-VR-Loader.exe
build\reshade-openxr-ui\ReShade-OpenXR-UI-Renderer-Test.exe
build\reshade-openxr-ui\layers\ReShade-OpenXR-UI-Layer.json
```

## Validation

Run these checks from the standalone repository root, in order. Replace the
simulator path with the location of your external OpenXR-Simulator bundle:

```powershell
build.bat
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\verify.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\smoke.ps1 -TestRoot C:\path\to\OpenXR-Simulator-test -Frames 30
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\capture_simulator_ui.ps1 -TestRoot C:\path\to\OpenXR-Simulator-test -Frames 1600
```

`verify.ps1` checks both add-on export contracts and requires an x64 GUI loader. `build.ps1` runs a synthetic process-start test that requires the loader to identify an allowlisted executable and inject both supplied modules before timeout. By default, the smoke test runs isolated D3D11 and D3D12 OpenXR Simulator legs, including a no-ReShade control for each API. The capture workflow also runs both APIs, drives the real OpenXR action path, and writes screenshots under `build\reshade-openxr-ui\simulator-captures\d3d11` and `simulator-captures\d3d12` for visual inspection.

Do not accept synthetic WARP rendering, log messages, hit tests, or screenshot existence as visual proof. Inspect every OpenXR Simulator capture and confirm that the panel, controls, labels, transient states, and preset transitions are visible and do not overlap.

Only the user launches a game for headset validation. A release candidate still needs real D3D11 and D3D12 OpenXR headset checks after automated validation.

## Release Package

Create the user-facing ZIP after the complete validation workflow:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\package.ps1
```

The package is written to `build\reshade-openxr-ui-release\ReShade-VR-UI-Addon.zip` with this layout:

```text
ReShade-OpenXR-UI.addon64
ReShade-SteamVR-UI.addon64
ReShade-VR-Loader.exe
openxr-layers\ReShade-OpenXR-UI-Layer.json
Install.cmd
Uninstall.cmd
INSTALL.md
LICENSE
THIRD-PARTY-NOTICES.txt
```

Rename the build output's `layers` directory to `openxr-layers` when assembling the ZIP. Keep the manifest exactly one directory below the DLL because its relative `library_path` is `..\ReShade-OpenXR-UI.addon64`.

The package script copies `Install.cmd`, `Uninstall.cmd`, and `INSTALL.md` from
this directory, plus `LICENSE` and `THIRD-PARTY-NOTICES.txt` from the
repository root, into the release root. The readable command scripts install
or remove both add-ons, the loader, and the manifest under
`%ProgramFiles%\ReShade VR UI Add-on`; they add or remove the machine-wide
OpenXR registration and current-user loader startup entry. They require
administrator access and preserve preferences and logs under
`%LOCALAPPDATA%\ReShade VR UI Add-on` during uninstall.

Do not ship the renderer test, PDB files, import libraries, object files, simulator captures, or ReShade itself.

## License

Project-owned source in this repository is licensed under the MIT License in
the repository root. The add-on statically compiles Dear ImGui and its DirectX
renderer backends, and uses ReShade, OpenXR, and OpenVR headers from the
separately obtained ReShade dependency tree. Those components retain their
upstream licenses. The release package includes
`THIRD-PARTY-NOTICES.txt` with the applicable attribution and license terms.