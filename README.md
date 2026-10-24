# ReShade VR UI Add-on

Control ReShade from inside your VR headset, without taking it off.

ReShade is a free program that adds visual effects, such as sharpening or
color changes, to games. This add-on lets you switch those effects on and off
and adjust them while you play in VR. ReShade itself is a separate download.

## What It Does

ReShade can already draw effects in VR games. What is missing is a way to
control ReShade from inside the headset:

- **SteamVR games:** ReShade already has its own menu in the SteamVR
  dashboard. The only missing piece was a `ReShadeVR.ini` file next to the
  game's `.exe`; without it, the ReShade icon does not appear in SteamVR. The
  SteamVR download includes that file (manual way), plus an add-on that writes
  it for you and adds a few extras (automatic way).
- **OpenXR games and UEVR:** ReShade has no menu in the headset, and no
  settings file fixes that. So this project adds its own panel in the headset
  to switch effects on and off and change them.

## Download And Install

**[⬇ Download and install steps (Releases page)](https://github.com/wyerdev/ReShade-VR-UI-Addon/releases/latest)**

The Releases page has the downloads and the full install steps for each kind
of game. Or go straight to the guide for your game; each one starts with its
download link:

| Your game | Download and install steps |
| --- | --- |
| OpenXR VR game, or an Unreal game played in VR with UEVR | [Install for OpenXR and UEVR](tools/reshade-openxr-ui/INSTALL.md) |
| SteamVR game | [Install for SteamVR](tools/reshade-openxr-ui/INSTALL-SteamVR.md) |
| Vulkan or 32-bit VR game (nothing to download) | [ReShade in Vulkan and 32-bit games](tools/reshade-openxr-ui/INSTALL-Vulkan-32bit.md) |

Every ZIP also has its install steps inside, in a file called `INSTALL.md`.

## For Developers

Everything below is for people who build or change the add-on. You do not need
it to install or use the add-on.

### Repository Boundary

The plugin source and tests live under `tools/reshade-openxr-ui`. Related
OpenXR integration test scripts live under `tools/` beside it. The official
ReShade source is fetched separately into `.deps/reshade`, which is ignored by
Git and never included in release packages.

The plugin uses the current ReShade checkout available locally. The dependency
setup script records the branch, commit, and submodule revisions used for a
build in `RESHADE-dependency.md`, but does not pin that revision.

### Setup

From this repository root, run the standard setup, build, and package flow:

```bat
build.bat
```

`build.bat` sets up the local ReShade dependency when it is missing, builds the
add-ons and the System-Wide SteamVR Listener, and creates the installable
packages. The setup script requires Git and
initializes ReShade's required submodules. The ReShade checkout must contain
its public headers plus the Dear ImGui, OpenXR SDK, and OpenVR dependency
trees.

For a clean rebuild and package, run `clean.bat`. It assumes the ReShade
dependency checkout has already been set up.

### Build

The root batch files are the standard build and installation entry points:

```bat
build.bat
build-addon.bat
build-listener.bat
clean.bat
clean-addon.bat
clean-listener.bat
install.bat
uninstall.bat
```

`build.bat` builds and packages both products by running `build-addon.bat`
and then `build-listener.bat`. `build-addon.bat` sets up the local ReShade
dependency when needed, builds the add-ons, and creates the add-on package.
`build-listener.bat` builds, tests and packages only the listener; it needs
the add-on build output, so run it after `build-addon.bat`. `clean.bat` runs
`clean-addon.bat` and then `clean-listener.bat`; each removes its own
generated build output, then rebuilds and repackages that product. They assume
the ReShade dependency checkout has already been set up. Run `install.bat` and
`uninstall.bat` as administrator. They manage the protected machine-wide
OpenXR registration and runtime files while preserving per-user preferences
and logs.

The wrapper builds against `.deps\reshade`, packages the release, and writes
generated files under `build\reshade-openxr-ui`. The build runs the private
renderer interaction test. To build ReShade itself,
use the build instructions and scripts inside the separate `.deps\reshade`
checkout first.

See [tools/reshade-openxr-ui/README.md](tools/reshade-openxr-ui/README.md) for
plugin architecture, release packaging, and simulator validation details.

The optional System-Wide SteamVR Listener is a separate program under
`tools/steamvr-listener` with its own package and installer. `install.bat` and
the add-on package do not include it; see
[tools/steamvr-listener/INSTALL.md](tools/steamvr-listener/INSTALL.md).

### Testing

`test.bat` runs everything in order (the build and its built-in tests, the quick
headset test and the menu screenshot test) and ends with one PASSED or FAILED
line per test. It exits with code 0 only when all of them pass.

```bat
test.bat C:\path\to\OpenXR-Simulator-test
```

The deterministic simulator bundle is external to this repository. Without a
path, `test.bat` uses `RESHADE_VR_UI_TEST_ROOT`, then
`.deps\openxr-simulator-test`, then an `OpenXR-Simulator-test` folder next to
this repository. The two simulator tests can also be run on their own from
PowerShell after `build.bat`:

```powershell
.\tools\reshade-openxr-ui\smoke.ps1 -TestRoot C:\path\to\OpenXR-Simulator-test -Frames 30
.\tools\reshade-openxr-ui\capture_simulator_ui.ps1 -TestRoot C:\path\to\OpenXR-Simulator-test
```

Inspect every captured simulator state. Synthetic renderer output, logs, hit
tests, and screenshot existence do not prove the headset UI is visually
correct.

Most users do not need the following two scripts. They are optional integration
tests for a full ReShade graphics proxy, not for testing this
repository's ReShade VR UI Add-on:

- `tools\test_openxr_simulator.ps1` copies a full `ReShade64.dll` into the
	isolated simulator as `dxgi.dll` and tests ReShade effect rendering.
- `tools\test_openxr_hubris.ps1` can stage the same kind of proxy for a
	user-invoked Hubris test. It never launches the game automatically.

The normal add-on validation is the `tools\reshade-openxr-ui\smoke.ps1` and
`capture_simulator_ui.ps1` workflow above. Those scripts use the
`build\reshade-openxr-ui\ReShade-OpenXR-UI.addon64` output produced by this
repository.

The Hubris/Pimax test script has an optional read-only status check:

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

### Release Package

After testing:

```bat
build.bat
```

`build.bat` creates the release packages after building the add-ons. For a
clean release build, use `clean.bat` first.

One GitHub release holds two ZIPs. Each ZIP contains only its own add-on and
its own install guide, renamed to `INSTALL.md`:

| ZIP (in `build\reshade-openxr-ui-release`) | For | Install guide |
| --- | --- | --- |
| `ReShade-VR-UI-Addon-OpenXR.zip` | OpenXR games and UEVR | `tools\reshade-openxr-ui\INSTALL.md` |
| `ReShade-VR-UI-Addon-SteamVR.zip` | SteamVR games, set up per game | `tools\reshade-openxr-ui\INSTALL-SteamVR.md` |

The release page text is built by `tools\release\New-ReleaseNotes.ps1`: first
`tools\reshade-openxr-ui\RELEASE_NOTES.md` (which ZIP to download), then each
install guide as a fold-out section, so users find the download and the steps
on one page while the steps are still written in one place. The System-Wide
SteamVR Listener is not part of the release for now.

#### Automatic releases

Releases are made by GitHub Actions
([build-release.yml](.github/workflows/build-release.yml)), not by hand:

- Every push to `main` and every pull request builds both ZIPs; they can be
  downloaded from the run's page.
- If the pushed commits include a `feat:`, `fix:`, `perf:`, `revert:` or
  breaking (`!`) commit, the same run publishes a GitHub Release with the two
  ZIPs. Other commit types wait for the next release.
- The version is a running release number and the date: release `v12` built on
  24 October 2026 is titled `v12 (2026-10-24)`, and its add-ons carry the
  Windows file version `2026.10.24.12`, which ReShade shows in its add-on list.
  Builds that are not released keep the date with `0` as the release number;
  local builds show `0.0.0.0`.
- The release text is `RELEASE_NOTES.md`, the install guides, then the
  changelog. The changelog lists only commits written as
  [Conventional Commits](https://www.conventionalcommits.org) (`feat: ...`,
  `fix: ...`, `docs: ...` and so on); other commit messages are left out.
- Each release also has `SHA256SUMS.txt`, a SHA-256 checksum (a fingerprint
  of the file) for each ZIP, also listed at the end of the release text. To
  check a download, run `Get-FileHash <zip> -Algorithm SHA256` in PowerShell
  and compare the result with the listed value.
- A push whose last commit message contains `(Skip)` is not built or released.
- To publish without a `feat` or `fix` commit (for example the first release),
  open the workflow on GitHub, choose **Run workflow** and tick the release box.

The packages contain only the plugin artifacts, installer, uninstaller,
manifest, install guide, project license, and third-party notices. They do not
contain ReShade source, the local ReShade checkout, build products, simulator
captures, or game test state. Keep the guides free of links relative to the
repository, because they are also read inside the ZIP.

## License

Project-owned source in this repository is licensed under the MIT License in
[LICENSE](LICENSE). The add-on is built against a separately obtained ReShade
checkout and compiles Dear ImGui's DirectX backends into the add-on binaries.
ReShade, Dear ImGui, the OpenXR SDK, and OpenVR remain under their upstream
licenses; see [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) for the terms
included with the release package.

