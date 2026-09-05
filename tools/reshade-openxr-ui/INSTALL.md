# Install ReShade VR UI Add-on

## Install ReShade

1. Download **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
2. Run the installer and select the game's main `.exe` file.
3. Select **DirectX 10/11/12**.
4. Enable **OpenXR** and complete the installation.

Repeat these ReShade steps for each game you want to enable in ReShade.

## Install the Add-on

1. Extract the complete ReShade VR UI Add-on ZIP.
2. Close games and VR runtime software, including SteamVR.
3. Right-click `Install.cmd` and select **Run as administrator**.

The runtime files and OpenXR registration are installed machine-wide so SteamVR can enable or disable the layer through its elevated API-layer control. The loader startup entry, preferences, and logs remain specific to your Windows account. Compatible 64-bit D3D11 and D3D12 games are supported through native OpenXR or SteamVR/OpenVR. Vulkan and OpenGL are not currently supported.

## SteamVR And OpenVR Games

SteamVR can run either OpenXR or the older OpenVR API. OpenXR games use the native headset quad. OpenVR games expose the focused controls inside ReShade's existing SteamVR dashboard.

No separate OpenVR installation is required. Keep **OpenXR enabled** in official ReShade Setup. `Install.cmd` installs a per-user background loader that reads the official central ReShade application list and waits for each enabled game to select a VR API. Native OpenXR games remain on the existing OpenXR layer path. When a game loads `openvr_api.dll`, the loader loads the exact official `C:\ProgramData\ReShade\ReShade64.dll` and then the central SteamVR UI companion. Nothing is copied into a game directory.

For **Propagation: Paradise Hotel**:

1. Select `Propagation_PH-Win64-Shipping.exe` in official full-add-on ReShade Setup.
2. Select **DirectX 10/11/12**, enable **OpenXR**, and complete Setup.
3. Run this package's `Install.cmd` once.
4. Launch the game normally. After effects finish loading, the companion opens ReShade's SteamVR dashboard.
5. Open **Add-ons** and expand **ReShade VR UI Add-on for SteamVR** to use the focused controls. ReShade's complete shader list remains on **Home**; `Generic Depth` and `ReShade VR UI Add-on for SteamVR` are add-ons, not shaders.

The loader starts immediately after installation and at Windows sign-in. Its diagnostic log is `%LOCALAPPDATA%\ReShade VR UI Add-on\ReShade-VR-Loader.log`. Some anti-cheat systems may reject process injection; the loader records that failure and does not bypass anti-cheat policy.

OpenVR games commonly create both a desktop effect runtime and a headset effect runtime. The SteamVR companion disables ReShade's built-in cross-runtime synchronization for that game before either runtime initializes; the SteamVR dashboard edits the headset runtime directly. This avoids recursive preset and uniform propagation between the two runtimes.

## No Shaders In The Panel

`ReShade effect runtime unavailable` means ReShade did not initialize VR effects. This is not caused by a missing `ReShadeVR.ini`. For OpenXR, check `ReShade.log` for a message such as `Skipping OpenXR session because it was created without a proxy Direct3D 12 device.` For OpenVR, check `ReShade-VR-Loader.log` for successful central loading and then check the game-local `ReShade.log`.

Do not add a game-local ReShade proxy. The loader injects the exact DLL referenced by official central OpenXR registration, so an OpenXR application resolves the same module rather than loading a second ReShade instance.

`No ReShade techniques found` means the effect runtime exists but did not discover any effects. ReShade normally falls back from missing keys in `ReShadeVR.ini` to `ReShade.ini`; check `EffectSearchPaths`, the installed shader files, and the active preset in those files. The add-on does not overwrite or synthesize per-game ReShade configuration.

## Update

Extract the new release, then right-click `Install.cmd` and select **Run as administrator** again. The installer preserves the layer's existing On or Off state.

## Uninstall

Right-click `Uninstall.cmd` and select **Run as administrator**. This stops the central loader, removes its per-user startup entry, removes the add-on's OpenXR registration and installed runtime files, and keeps preferences and logs. It does not change official ReShade, games, effects, presets, or game configuration.

Run `Uninstall.cmd` when you no longer use the add-on, including when uninstalling ReShade. If ReShade is removed first, the add-on remains registered but stays inactive; running `Uninstall.cmd` removes that remaining registration completely.
