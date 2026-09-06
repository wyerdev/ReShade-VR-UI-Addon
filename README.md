# ReShade VR UI Add-on

This repository contains the ReShade VR UI Add-on and its OpenXR validation
harnesses. ReShade itself is not part of this repository.

## Repository Boundary

The plugin source and tests live under `tools/reshade-openxr-ui`. Related
OpenXR integration harnesses live under `tools/` beside it. The official
ReShade source is fetched separately into `.deps/reshade`, which is ignored by
Git and never included in release packages.

The plugin uses the current ReShade checkout available locally. The dependency
setup script records the branch, commit, and submodule revisions used for a
build in `RESHADE-dependency.md`, but does not pin that revision.

## Setup

From this repository root, run the standard setup, build, and package flow:

```bat
build.bat
```

`build.bat` sets up the local ReShade dependency when it is missing, builds the
add-ons, and creates the installable package. The setup script requires Git and
initializes ReShade's required submodules. The ReShade checkout must contain
its public headers plus the Dear ImGui, OpenXR SDK, and OpenVR dependency
trees.

For a clean rebuild and package, run `clean.bat`. It assumes the ReShade
dependency checkout has already been set up.

## Build

The root batch files are the standard build and installation entry points:

```bat
build.bat
clean.bat
install.bat
uninstall.bat
```

`build.bat` sets up the local ReShade dependency when needed, builds the
add-ons, and creates the installable package. `clean.bat` removes the generated
build output, then rebuilds and recreates the package; it assumes the ReShade
dependency checkout has already been set up. Run `install.bat` and
`uninstall.bat` as administrator. They manage the protected machine-wide
OpenXR registration and runtime files while preserving per-user preferences
and logs.

The wrapper builds against `.deps\reshade`, packages the release, and writes
generated files under `build\reshade-openxr-ui`. The build runs the private
renderer interaction test and the central loader test. To build ReShade itself,
use the build instructions and scripts inside the separate `.deps\reshade`
checkout first.

See [tools/reshade-openxr-ui/README.md](tools/reshade-openxr-ui/README.md) for
plugin architecture, release packaging, and simulator validation details.

## Validation

The deterministic simulator bundle is external to this repository. Supply its
path explicitly:

```powershell
.\tools\reshade-openxr-ui\smoke.ps1 -TestRoot C:\path\to\OpenXR-Simulator-test -Frames 30
.\tools\reshade-openxr-ui\capture_simulator_ui.ps1 -TestRoot C:\path\to\OpenXR-Simulator-test -Frames 1600
```

These validation scripts do not have batch-file wrappers and must be run from
PowerShell after `build.bat`.

Inspect every captured simulator state. Synthetic renderer output, logs, hit
tests, and screenshot existence do not prove the headset UI is visually
correct.

Most users do not need the following two scripts. They are optional integration
harnesses for testing a full ReShade graphics proxy, not for testing this
repository's ReShade VR UI Add-on:

- `tools\test_openxr_simulator.ps1` copies a full `ReShade64.dll` into the
	isolated simulator as `dxgi.dll` and tests ReShade effect rendering.
- `tools\test_openxr_hubris.ps1` can stage the same kind of proxy for a
	user-invoked Hubris test. It never launches the game automatically.

The normal add-on validation is the `tools\reshade-openxr-ui\smoke.ps1` and
`capture_simulator_ui.ps1` workflow above. Those scripts use the
`build\reshade-openxr-ui\ReShade-OpenXR-UI.addon64` output produced by this
repository.

The Hubris/Pimax harness is an optional read-only status check:

```powershell
.\tools\test_openxr_hubris.ps1 -Action Status -GameRoot C:\path\to\Hubris
```

For `Install`, `Run`, or `Capture`, pass the path to an existing full
`ReShade64.dll` with `-SourceDll`. This is a ReShade runtime DLL from a
separate ReShade build or installation; it is not
`ReShade-OpenXR-UI.addon64`, and `setup-reshade.ps1` does not build it:

```powershell
.\tools\test_openxr_simulator.ps1 -Action Capture -TestRoot C:\path\to\OpenXR-Simulator-test -SourceDll C:\path\to\ReShade64.dll
.\tools\test_openxr_hubris.ps1 -Action Prepare -GameRoot C:\path\to\Hubris -SourceDll C:\path\to\ReShade64.dll
```

The migration and build scripts never launch a game or modify an existing
ReShade checkout.

## Release Package

After validation:

```bat
build.bat
```

`build.bat` creates the release package after building the add-ons. For a clean
release build, use `clean.bat` first.

The package contains only the plugin artifacts, installer, uninstaller,
manifest, install guide, project license, and third-party notices. It does not
contain ReShade source, the local ReShade checkout, build products, simulator
captures, or game test state.

## License

Project-owned source in this repository is licensed under the MIT License in
[LICENSE](LICENSE). The add-on is built against a separately obtained ReShade
checkout and compiles Dear ImGui's DirectX backends into the add-on binaries.
ReShade, Dear ImGui, the OpenXR SDK, and OpenVR remain under their upstream
licenses; see [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) for the terms
included with the release package.

## Installation

For end-user installation, see [tools/reshade-openxr-ui/INSTALL.md](tools/reshade-openxr-ui/INSTALL.md).
Official ReShade with full add-on support remains a separate prerequisite.
