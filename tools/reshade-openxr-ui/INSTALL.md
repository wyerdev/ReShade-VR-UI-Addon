# Install ReShade VR UI Add-on

Works with 64-bit DirectX 11 and 12 games. Vulkan and OpenGL are not supported.

## Native OpenXR Games

- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- In ReShade Setup, pick the game's main `.exe`, choose **DirectX 10/11/12**, and enable **OpenXR**.
- Close games and SteamVR, then right-click `Install.cmd` and choose **Run as administrator** (once for all games).
- Start the game normally. The panel appears in the headset; `L3 + R3` shows or hides it.

## SteamVR (OpenVR) Games

This is the normal way to install a ReShade add-on. `Install.cmd` is not needed.

- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- In ReShade Setup, pick the game's main `.exe`, choose **DirectX 10/11/12**, and leave **OpenXR** unticked. ReShade Setup then puts ReShade next to the game's `.exe`.
- If ReShade was already installed for this game with **OpenXR** ticked, choose **Uninstall** in ReShade Setup first, then install again with it unticked.
- Copy `ReShade-SteamVR-UI.addon64` from this release into the folder with the game's `.exe`. Do not copy `ReShade-OpenXR-UI.addon64`.
- Start the game normally. ReShade's SteamVR dashboard opens after effects load.
- Open **Add-ons** > **ReShade VR UI Add-on for SteamVR** for the controls.

### Alternative: Automatic Loader

Instead of the steps above, `Install.cmd` can also install a small loader program. It is installed only if you answer **Y** when `Install.cmd` asks. It starts with Windows and adds ReShade and the add-on to SteamVR games automatically, so nothing is copied into game folders.

- In ReShade Setup, pick the game's main `.exe`, choose **DirectX 10/11/12**, and enable **OpenXR**.
- Close games and SteamVR, then right-click `Install.cmd` and choose **Run as administrator** (once for all games). Answer **Y** when it asks to install the automatic SteamVR loader.
- Start the game normally, then open **Add-ons** > **ReShade VR UI Add-on for SteamVR**.
- Loader log: `%LOCALAPPDATA%\ReShade VR UI Add-on\ReShade-VR-Loader.log`.

## UEVR (Unreal Games)

- If ReShade is installed in the game folder (a normal desktop install), turn it off first. Either run ReShade Setup, pick the game and choose **Uninstall**, or rename ReShade's `dxgi.dll` next to the game's `.exe` (for example to `dxgi.dll.off`; rename it back for flat play). UEVR needs ReShade only as an OpenXR layer.
- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- In ReShade Setup, pick the game's `...-Win64-Shipping.exe`, choose **DirectX 10/11/12**, and enable **OpenXR**.
- Close games and SteamVR, then right-click `Install.cmd` and choose **Run as administrator** (once for all games).
- Start the game, then inject with UEVR set to **OpenXR** (not OpenVR).
- The panel appears in the headset. `ReShade effect runtime unavailable` may show for a moment, then effects load.
- Switch effects on in the panel. A new game starts with none enabled.

## Update Or Uninstall

- Update: close games and SteamVR, extract the new release, then right-click its `Install.cmd` and choose **Run as administrator**. It asks about the loader again; answering **N** removes a loader installed earlier.
- For SteamVR games set up by copying, also replace `ReShade-SteamVR-UI.addon64` in each game folder with the new one.
- Uninstall: close games and SteamVR, then right-click `Uninstall.cmd` and choose **Run as administrator**. Presets, logs and games are not touched.
- For SteamVR games set up by copying, delete `ReShade-SteamVR-UI.addon64` from each game folder.
- If no effects appear, check `ReShade.log` next to the game's `.exe`.

## Where It Installs And Why Admin Rights Are Needed

- Files go to `C:\Program Files\ReShade VR UI Add-on`: the OpenXR add-on and the OpenXR layer file in `openxr-layers`. If you choose the automatic loader, the SteamVR add-on and the loader (`ReShade-VR-Loader.exe`) go there too.
- Admin rights are needed for two things: writing to `C:\Program Files`, and registering the OpenXR layer for all users under `HKEY_LOCAL_MACHINE\Software\Khronos\OpenXR\1\ApiLayers\Implicit`. This is how OpenXR games find the add-on.
- If you choose the automatic loader, it is set to start with Windows for your account (`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`).
- Settings and logs are kept in `%LOCALAPPDATA%\ReShade VR UI Add-on`. Uninstalling does not remove them.
- `Install.cmd` and `Uninstall.cmd` never change game folders. The only file placed in a game folder is the SteamVR add-on you copy there yourself.
