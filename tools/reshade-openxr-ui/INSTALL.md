# Install ReShade VR UI Add-on

Works with 64-bit DirectX 11 and 12 games. Vulkan and OpenGL are not supported.

## Native OpenXR Games

- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- In ReShade Setup, pick the game's main `.exe`, choose **DirectX 10/11/12**, and enable **OpenXR**.
- Close games and SteamVR, then right-click `Install.cmd` and choose **Run as administrator** (once for all games).
- Start the game normally. The panel appears in the headset; `L3 + R3` shows or hides it.

## SteamVR (OpenVR) Games

- Do the same ReShade Setup steps as above (keep **OpenXR** enabled).
- Run `Install.cmd` as administrator once, if not done already.
- Start the game normally. ReShade's SteamVR dashboard opens after effects load.
- Open **Add-ons** > **ReShade VR UI Add-on for SteamVR** for the controls.
- Loader log: `%LOCALAPPDATA%\ReShade VR UI Add-on\ReShade-VR-Loader.log`.

## UEVR (Unreal Games)

- If ReShade is installed in the game folder (a normal desktop install), turn it off first. Either run ReShade Setup, pick the game and choose **Uninstall**, or rename ReShade's `dxgi.dll` next to the game's `.exe` (for example to `dxgi.dll.off`; rename it back for flat play). UEVR needs ReShade only as an OpenXR layer.
- Do the same ReShade Setup steps for the game's `...-Win64-Shipping.exe` (keep **OpenXR** enabled).
- Run `Install.cmd` as administrator once, if not done already.
- Start the game, then inject with UEVR set to **OpenXR** (not OpenVR).
- The panel appears in the headset. `ReShade effect runtime unavailable` may show for a moment, then effects load.
- Switch effects on in the panel. A new game starts with none enabled.

## Update Or Uninstall

- Update: extract the new release and run `Install.cmd` as administrator again.
- Uninstall: run `Uninstall.cmd` as administrator. Presets, logs and games are not touched.
- If no effects appear, check `ReShade.log` next to the game's `.exe`.
