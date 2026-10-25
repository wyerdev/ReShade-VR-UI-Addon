# Developer Guide

How to build, test and release the ReShade VR UI Add-on. Players do not need this; see [README.md](README.md).

## Repository Boundary

The plugin source and tests live under `tools/reshade-openxr-ui`. Related
OpenXR integration test scripts live under `tools/` beside it. The official
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
add-ons and the System-Wide SteamVR Listener, and creates the installable
packages. The setup script requires Git and
initializes ReShade's required submodules. The ReShade checkout must contain
its public headers plus the Dear ImGui, OpenXR SDK, and OpenVR dependency
trees.

For a clean rebuild and package, run `clean.bat`. It assumes the ReShade
dependency checkout has already been set up.

## Build

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

## Testing

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

The menu pictures in the README and on the release page come from
`tools\reshade-openxr-ui\readme_images.ps1`. Run it after a menu design change
and check the two pictures it writes to `tools\reshade-openxr-ui\images`.

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

## Release Package

After testing:

```bat
build.bat
```

`build.bat` creates the release packages after building the add-ons. For a
clean release build, use `clean.bat` first.

One GitHub release holds one ZIP and one plain file:

| File (in `build\reshade-openxr-ui-release`) | For | Install guide |
| --- | --- | --- |
| `ReShade-VR-UI-Addon-OpenXR<suffix>.zip` | OpenXR games and UEVR | `tools\reshade-openxr-ui\INSTALL.md` (inside the ZIP as `INSTALL.md`) |
| `ReShadeVR.ini` | SteamVR games, optional smaller menu text | `tools\reshade-openxr-ui\INSTALL-SteamVR.md` |

The SteamVR add-on (`ReShade-SteamVR-UI.addon64`) is still built but not
released for now; its code stays. `ReShadeVR.ini` is not zipped and has no
suffix (ReShade only reads that exact name). ReShade shows its SteamVR icon
without it. Because its name never changes, the guides link to it with
`releases/latest/download/ReShadeVR.ini`.

`<suffix>` is the release number and date, for example
`ReShade-VR-UI-Addon-OpenXR-v12-2026-10-24.zip`. It comes from the
`RESHADE_VR_UI_RELEASE_SUFFIX` environment variable, which the release workflow
sets. Local builds leave it empty, so their ZIPs have no version. The unpacked
folders next to the ZIPs never get the suffix, because `install.bat` uses them.

The release page text is built by `tools\release\New-ReleaseNotes.ps1` from
`tools\reshade-openxr-ui\RELEASE_NOTES.md`: a download table (its
`{{OPENXR_DOWNLOAD}}` and `{{VR_INI_DOWNLOAD}}`
placeholders become direct links to this release's ZIP and `ReShadeVR.ini`) with a link to each install guide at this
release's tag, then the features with menu pictures, the changelog and the
checksums. The steps are written only in the install guides. The System-Wide
SteamVR Listener is not part of the release for now.

The README's download button opens the newest release page, not a file:
GitHub's "newest file" link (`releases/latest/download/<name>`) only works when
the file name never changes, and the versioned names change every release.

### Automatic releases

Releases are made by GitHub Actions
([build-release.yml](.github/workflows/build-release.yml)), not by hand:

- Every push to `main` (and every manual run on `main`) builds both ZIPs and
  publishes them as a GitHub Release. The only way to skip a push is
  `(Skip)` in the first line of the last commit message (any letter case),
  which skips the build too; `(Skip)` further down the message is ignored.
  Nothing is
  built without being released, so pull requests and other branches are not
  built.
- The version is a running release number and the date: release `v12` built on
  24 October 2026 is titled `v12 (2026-10-24)`, and its add-ons carry the
  Windows file version `2026.10.24.12`, which ReShade shows in its add-on list.
  Its ZIPs end in `-v12-2026-10-24.zip`. Local builds show `0.0.0.0`.
- The release text is `RELEASE_NOTES.md`, the install guides, then the
  changelog. The changelog lists only commits written as
  [Conventional Commits](https://www.conventionalcommits.org) (`feat: ...`,
  `fix: ...` and so on); `docs:` commits and other commit messages are left
  out.
  It covers every commit since the last release that still exists on GitHub,
  so deleting releases folds their changes into the next one. The release
  number still counts up from the highest `vN` tag. Players read this list, so write the first line of a commit message as what
  they notice, in everyday words: `fix: download links on the release page
  work again`, not `fix: download names without brackets`.
- Each release also has `SHA256SUMS.txt`, a SHA-256 checksum (a fingerprint
  of the file) for each ZIP, also listed at the end of the release text. To
  check a download, run `Get-FileHash <zip> -Algorithm SHA256` in PowerShell
  and compare the result with the listed value.
- ZIP names use only letters, digits, `-`, `_` and `.`. GitHub renames uploaded
  files with other characters (brackets became dots in v2), which breaks the
  download links, so `package.ps1` refuses them.

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
