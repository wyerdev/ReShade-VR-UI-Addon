# Official ReShade Injector Research

Date recorded: 2026-09-06

Status: Research only. The injector design described here was not approved and
has not been implemented in this repository.

## Purpose

This note preserves the investigation into whether the official ReShade
`inject64.exe` can provide global ReShade activation for legacy OpenVR games
without restoring the deleted custom `ReShade-VR-Loader.exe`, copying files
into game directories, or changing game-local configuration.

The desired product remains one ReShade VR UI add-on with:

- OpenXR activation through the globally registered OpenXR API layer.
- Legacy OpenVR/SteamVR support through ReShade's SteamVR dashboard.
- Installation under `%ProgramFiles%\ReShade VR UI Add-on`.
- No custom loader executable.
- No game-local add-on copies, proxy DLLs, configuration edits, or launch-option
  changes.
- No hidden target-environment workaround.

## Current Runtime Evidence

OpenXR activation currently works.

The latest observed Propagation: Paradise Hotel launch used legacy OpenVR, but
ReShade did not activate:

- The existing game-local `ReShade.log` was not updated by that launch.
- No new add-on diagnostics were written.
- No DirectX ReShade proxy DLL was present beside the game executable.
- Both machine-wide OpenXR layers were enabled, but the process did not load
  `openxr_loader.dll`.
- The installed `ReShade-SteamVR-UI.addon64` matched the diagnostic build.
- No legacy custom loader process was running.

An older successful OpenVR launch showed that:

- ReShade initialized.
- `ReShade-SteamVR-UI.addon64` registered externally.
- The OpenVR headset runtime was detected.
- 65 techniques and 401 uniforms were enumerated.
- The ReShade SteamVR dashboard was requested.

Inspection of the deleted custom loader source showed that this older success
depended on process injection using `OpenProcess`, `VirtualAllocEx`,
`WriteProcessMemory`, `CreateRemoteThread`, and `LoadLibraryW`.

## Verified ReShade Behavior

### Add-ons do not activate ReShade

ReShade's add-on manager runs only after ReShade is already loaded into the
target process. It searches for `.addon64` files in `g_reshade_base_path`, or
in the path selected by `[ADDON] AddonPath`.

Relevant upstream source:

- `.deps/reshade/source/addon_manager.cpp`
- `.deps/reshade/source/dll_main.cpp`
- `.deps/reshade/include/reshade.hpp`

Therefore, installing an add-on globally or setting `AddonPath` cannot cause
ReShade to enter an OpenVR process. `AddonPath` only controls discovery after
ReShade has activated.

OpenXR is different because the Windows OpenXR loader discovers registered API
layers before the application creates its OpenXR instance. Legacy OpenVR has no
equivalent system API-layer registry.

### Injected ReShade defaults to the game directory

When ReShade is loaded under the generic name `ReShade64.dll`, rather than as a
graphics proxy such as `dxgi.dll`, upstream ReShade normally selects the target
executable directory as `g_reshade_base_path`.

That base path controls locations including:

- `ReShade.ini`
- `ReShadeVR.ini`
- `ReShade.log`
- presets and effect search paths
- add-on discovery

The base path can be changed through either:

- `[INSTALL] BasePath` in a game-local `ReShade.ini`; or
- the target process environment variable `RESHADE_BASE_PATH_OVERRIDE`.

The first choice directly conflicts with the no-game-local-configuration
requirement. The second variable must exist inside the target process before
official ReShade is loaded.

The machine-wide ReShade directory was inspected read-only. It contained the
official 32-bit and 64-bit runtime DLLs, layer manifests, and the application
allowlist, but it did not contain a shared `ReShade.ini`, presets, or shader
tree suitable for immediate use as a global ReShade base path.

## Official Injector Findings

Official downloads supplied for the investigation:

- `https://reshade.me/downloads/inject64.exe`
- `https://reshade.me/downloads/inject32.exe`

Historical properties observed on 2026-09-06:

| File | Size | SHA-256 | Architecture | Signature |
| --- | ---: | --- | --- | --- |
| `inject64.exe` | 14,336 bytes | `0B61CED21023EF811578B8B3991A599B14C07D48A9800CA04993D305E42D41FC` | x64 | Unsigned |
| `inject32.exe` | 11,776 bytes | `37A26093FCD0866365048F98A5F1C1DED9CAD94FE23B7EF6A1FFE3AB59D392B4` | x86 | Unsigned |

These values are historical evidence only. The proposed design did not require
version or hash pinning.

The corresponding upstream source is:

- `.deps/reshade/tools/injector.cpp`

The official injector:

- Accepts exactly one target executable name or process ID.
- Waits for the named process when given an executable name.
- Uses the injector executable's own directory, not its current working
  directory, to find the payload.
- Loads only an adjacent file named `ReShade64.dll` or `ReShade32.dll`.
- Sets `RESHADE_DISABLE_LOADING_CHECK=1` inside the target process.
- Uses remote allocation, `WriteProcessMemory`, and `CreateRemoteThread` to call
  `LoadLibraryW`.
- Waits for the remote load operation and reports success or failure.
- Does not discover or inject `.addon64` files.
- Does not set `RESHADE_BASE_PATH_OVERRIDE`.

The user's trust decision concerned the official `reshade.me` injector. It did
not approve a particular coordinator, launch workflow, base-path strategy, or
custom bootstrap implementation.

## Why One Normal Injector Call Is Insufficient

Running official `inject64.exe` with official `ReShade64.dll` can activate
ReShade, but it does not load this project's custom add-on.

Placing `ReShade-SteamVR-UI.addon64` beside the injector does not help because
the injector ignores `.addon64` files. ReShade also does not search beside its
own DLL in this injection mode: its default base path is the target executable
directory.

Using `[ADDON] AddonPath` would still require a game-local `ReShade.ini`, and it
would not solve initial ReShade activation.

## Two Official Injector Calls

The official injector's fixed payload name makes two isolated directories
possible:

```text
injector\addon\
    inject64.exe          Official ReShade injector
    ReShade64.dll         Custom VR UI add-on under the required payload name

injector\reshade\
    inject64.exe          Official ReShade injector
    ReShade64.dll         Official full-add-on ReShade runtime
```

This uses the official injector for both remote `LoadLibraryW` operations. It
does not require a custom injector executable.

The DLL filename used for injection does not determine the displayed ReShade
add-on name. The add-on's exported `NAME` value remains responsible for its
identity.

Two calls alone are not yet a complete compliant solution. Their order changes
which problem remains.

### Official ReShade first, custom add-on second

Advantages:

- The custom add-on can immediately find ReShade's exported add-on API and
  register itself.
- The ordering is easy for a foreground PowerShell script to enforce.

Problems:

- ReShade selects the game directory as its base path before the add-on is
  loaded.
- ReShade may create or update game-local configuration and logs.
- If the VR effect runtime initializes before the second injection, the add-on
  misses its `init_effect_runtime` and effect-reload events.

A prototype late-attachment fallback was compiled successfully during the
investigation. It allowed the add-on to recognize an existing windowless VR
runtime on a later `reshade_present` event and rebuild its effect inventory.
The prototype was removed after the base-path violation was discovered.

### Custom add-on first, official ReShade second

Advantages:

- The custom DLL can set `RESHADE_BASE_PATH_OVERRIDE` inside the target process
  before official ReShade initializes.
- The same already-loaded custom DLL can later register with ReShade.
- This can avoid all game-directory writes.

Problems and required work:

- The add-on must wait for ReShade to appear and then register itself. It must
  not load ReShade or perform injection itself.
- Registration and runtime adoption must be safe if graphics initialization
  races with the second official injector call.
- A writable shared ReShade base directory must be selected.
- That directory needs usable ReShade configuration, effect paths, presets,
  and shader resources. The inspected machine-wide runtime directory does not
  currently provide them.
- Importing or translating an existing game's configuration into a per-user
  shadow directory would need to be explicit and documented, not silent.

This was the most promising design that preserved the no-custom-executable and
no-game-local-write boundaries, but it was not approved.

## Coordinator Choices Considered

### Foreground wait-by-name PowerShell script

The user starts the script and then launches the game normally. The first
official injector waits for the executable name, and the script starts the
second injector only after the first exits successfully.

Properties:

- Does not launch the game.
- Does not modify launch options.
- Does not remain resident after both injections.
- Contains no injection implementation; it only invokes official executables.
- Cannot place an environment variable into an independently launched game
  without the add-on-first bootstrap described above.

### Script launches the game suspended

A script could create the target process suspended, set up the required
environment, perform both official injections by process ID, and then resume
the main thread.

This gives deterministic ordering but changes the launch workflow and makes the
script launch the game. It was not approved and should not be implemented
without an explicit change to that boundary.

### Background watcher or service

A persistent watcher could detect games and coordinate injection. This was
rejected because the product requirements exclude a hidden background process
and the old custom loader must not be restored in another form.

## Shared Configuration Question

Avoiding game-local writes requires ReShade to use a base directory outside the
game. A possible per-user layout is:

```text
%LOCALAPPDATA%\ReShade VR UI Add-on\Games\<game>\
```

Each game could have a writable shadow `ReShade.ini`, `ReShadeVR.ini`, logs,
and presets there. Existing game configuration could be read and explicitly
imported without modifying the source files.

This is not ready to implement because several product decisions remain:

- Whether an explicit read-only import from existing game configuration is
  acceptable.
- Whether relative effect and texture paths should be translated to absolute
  paths during import.
- Whether shaders should remain in their existing read-only locations or be
  installed once into a shared shader directory.
- How users select or initialize a per-game shared configuration.
- Whether the configuration is keyed by executable name or full executable
  path.

Using `%ProgramFiles%\ReShade VR UI Add-on` as the writable ReShade base is not
appropriate because normal game processes should not write to Program Files.
Using the existing machine-wide ReShade runtime directory is also insufficient
until it has a deliberate shared configuration and shader layout.

## One Custom DLL Question

The repository currently builds two frontend DLLs over the same state model:

- `ReShade-OpenXR-UI.addon64`, which is also an OpenXR API layer.
- `ReShade-SteamVR-UI.addon64`, which integrates with ReShade's SteamVR
  dashboard.

The injector design can inject exactly one custom DLL into an OpenVR process,
but the installed product still contains two frontend binaries. Producing one
physical custom DLL for both OpenXR and OpenVR would require combining the
OpenXR renderer and SteamVR dashboard frontend into one build.

That consolidation needs care because:

- The OpenXR frontend compiles a private Dear ImGui implementation.
- The SteamVR frontend uses ReShade's exported Dear ImGui function table.
- Combining those two ImGui ownership models in one DLL may introduce symbol or
  context conflicts.
- OpenXR API-layer loading can happen before ReShade exists, while OpenVR
  injection happens around ReShade activation.

No combined-DLL implementation was attempted or retained.

## Proposed Future Design

If explicitly approved later, the most boundary-compatible design to prototype
is:

1. Keep OpenXR activation unchanged through the registered API layer.
2. Install the current official `inject64.exe` from `reshade.me` without a
   version or hash pin.
3. Use a visible foreground PowerShell coordinator that waits for an executable
   name and never launches a game.
4. First use official `inject64.exe` to load the custom add-on under the
   injector-required `ReShade64.dll` filename.
5. Have the custom add-on set an explicitly documented per-user ReShade base
   path. It must not inject or load ReShade itself.
6. Use a second copy of official `inject64.exe` to load the official full-add-on
   ReShade runtime from a separate directory.
7. Have the already-loaded add-on register when ReShade becomes available.
8. Add late runtime adoption so a missed initialization event cannot leave the
   SteamVR dashboard disconnected.
9. Keep all writable ReShade state outside game and Program Files directories.
10. Provide an explicit configuration-import/setup step rather than silently
    copying or rewriting game configuration.
11. Later evaluate combining the OpenXR and SteamVR frontends into one physical
    custom DLL.

## Approval Needed Before Implementation

Future implementation should not begin until the user explicitly decides all
of the following:

1. Whether a foreground PowerShell coordinator invoking official
   `inject64.exe` twice is acceptable.
2. Whether the custom add-on may set `RESHADE_BASE_PATH_OVERRIDE` inside the
   target before official ReShade loads.
3. Where shared writable ReShade configuration, presets, and logs should live.
4. How effects and textures should be made available from that shared
   configuration.
5. Whether an explicit read-only import of existing game configuration is
   acceptable.
6. Whether one physical combined frontend DLL is mandatory before testing the
   injector path.

## Validation Requirements For Future Work

Do not claim SteamVR success from OpenXR Simulator results. A future
implementation should be validated in this order:

1. Build both supported paths and run the private renderer interaction test.
2. Run `verify.ps1` and confirm the intended exports and diagnostic strings.
3. Run the OpenXR D3D11 and D3D12 smoke tests as shared-core regression checks
   only.
4. Confirm the release contains no custom loader executable.
5. Confirm the coordinator invokes only official `inject64.exe` binaries.
6. Confirm no game-local file is created or modified by the injector workflow.
7. Let the user launch the OpenVR game.
8. Inspect the resulting ReShade and SteamVR logs directly.
9. Require a new ReShade log containing the SteamVR add-on registration,
   windowless OpenVR runtime, effect inventory, and dashboard diagnostics.
10. Treat a headset observation of the dashboard as the final visual proof.

## Repository State When Research Was Archived

Before this note was added:

- The provisional coordinator, installer integration, and late-runtime adoption
  prototype had been removed.
- The existing staged and unstaged user changes were preserved.
- `build.bat` passed.
- The private renderer interaction test passed.
- `verify.ps1` passed for both add-on artifacts.
- Editor diagnostics reported no errors.
- `git diff --check` passed.
- The release package contained no official injector, coordinator, or custom
  loader executable.
- No game was launched or modified during this investigation.