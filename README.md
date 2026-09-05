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

From this repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\setup-reshade.ps1 -Action Setup
```

To fetch the latest ReShade revision later:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\setup-reshade.ps1 -Action Update
```

An existing ReShade checkout can be used instead:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-openxr-ui.ps1 -ReShadePath C:\path\to\reshade
```

The setup script requires Git and initializes ReShade's required submodules.
The ReShade checkout must contain its public headers plus the Dear ImGui,
OpenXR SDK, and OpenVR dependency trees.

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
dependency checkout has already been set up. `install.bat` runs the generated
package installer. `uninstall.bat` removes the installed current-user
registration and add-on files while preserving preferences and logs.

Build and package the plugin from the repository root:

```bat
build.bat
```

The wrapper builds against `.deps\reshade`, packages the release, and writes
generated files under `build\reshade-openxr-ui`. The build runs the private
renderer interaction test and the central loader test. To build ReShade itself,
use the build instructions and scripts inside the separate `.deps\reshade`
checkout first.

For a clean rebuild and package:

```bat
clean.bat
```

The direct PowerShell build wrapper is also available for advanced use and
custom output paths:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-openxr-ui.ps1 -ReShadePath C:\path\to\reshade
```

That PowerShell wrapper builds the add-ons but does not create the release
package. Use `tools\reshade-openxr-ui\package.ps1` afterward when packaging is
needed.

See [tools/reshade-openxr-ui/README.md](tools/reshade-openxr-ui/README.md) for
plugin architecture, release packaging, and simulator validation details.

## Validation

The deterministic simulator bundle is external to this repository. Supply its
path explicitly:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\smoke.ps1 -TestRoot C:\path\to\OpenXR-Simulator-test -Frames 30
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\capture_simulator_ui.ps1 -TestRoot C:\path\to\OpenXR-Simulator-test -Frames 1600
```

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
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\test_openxr_hubris.ps1 -Action Status -GameRoot C:\path\to\Hubris
```

For `Install`, `Run`, or `Capture`, pass the path to an existing full
`ReShade64.dll` with `-SourceDll`. This is a ReShade runtime DLL from a
separate ReShade build or installation; it is not
`ReShade-OpenXR-UI.addon64`, and `setup-reshade.ps1` does not build it:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\test_openxr_simulator.ps1 -Action Capture -TestRoot C:\path\to\OpenXR-Simulator-test -SourceDll C:\path\to\ReShade64.dll
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\test_openxr_hubris.ps1 -Action Prepare -GameRoot C:\path\to\Hubris -SourceDll C:\path\to\ReShade64.dll
```

The migration and build scripts never launch a game or modify an existing
ReShade checkout.

## Release Package

After validation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\package.ps1
```

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
