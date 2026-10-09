# Install ReShade VR UI Add-on

Works with 64-bit DirectX 11, DirectX 12 and OpenGL games that use OpenXR, and with DirectX 11 and 12 SteamVR (OpenVR) games. Vulkan OpenXR games are not supported. For 32-bit and Vulkan SteamVR games, see the tip below.

## Native OpenXR Games (DirectX 11, DirectX 12 and OpenGL)

- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- In ReShade Setup, pick the game's main `.exe`, choose its graphics API (**DirectX 10/11/12** or **OpenGL**), and enable **OpenXR**.
- Close games and SteamVR, then right-click `Install-OpenXR.cmd` and choose **Run as administrator** (once for all OpenXR games).
- Start the game normally. The panel appears in the headset; `L3 + R3` shows or hides it.

## OpenGL Games: Depth Buffer And FPS

OpenGL games (for example SOMA-VR) install and start the same way. Two things are different:

- Effects appear once the game shows its normal 3D view. A flat title screen may show `ReShade effect runtime unavailable` until then.
- ReShade's depth buffer helper (**Generic Depth**) costs much more FPS in OpenGL; in SOMA-VR about 30 FPS. It lets shaders know how far away things are.

For more FPS, untick **Depth buffer (off = faster in OpenGL)** in the headset panel, then restart the game. Shaders that need depth (for example MXAO, depth of field, depth fog) then stop working; the game itself is not affected. Tick it again and restart to get them back. The setting is saved per game.

## SteamVR (OpenVR) Games

This is the normal way to install a ReShade add-on. The OpenXR installer is not needed.

- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- In ReShade Setup, pick the game's main `.exe`, choose **DirectX 10/11/12**, and leave **OpenXR** unticked. ReShade Setup then puts ReShade next to the game's `.exe`.
- If ReShade was already installed for this game with **OpenXR** ticked, choose **Uninstall** in ReShade Setup first, then install again with it unticked.
- Copy `ReShade-SteamVR-UI.addon64` from this release into the folder with the game's `.exe`. Do not copy `ReShade-OpenXR-UI.addon64`.
- Start the game normally. ReShade's SteamVR dashboard opens after effects load.
- Open **Add-ons** > **ReShade VR UI Add-on for SteamVR** for the controls.

### Optional: System-Wide SteamVR Listener

The listener is a separate download named `ReShade-System-Wide-SteamVR-Listener.zip`. It starts with Windows and automatically adds ReShade and this add-on to compatible SteamVR games selected in ReShade Setup. Use it only if you want system-wide operation without copying the add-on into each game folder.

- In ReShade Setup, pick the game's main `.exe`, choose **DirectX 10/11/12**, and enable **OpenXR**.
- Extract the listener ZIP, close games and SteamVR, then right-click `Install-System-Wide-SteamVR-Listener.cmd` and choose **Run as administrator**.
- Start the game normally, then open **Add-ons** > **ReShade VR UI Add-on for SteamVR**.
- Listener log: `%LOCALAPPDATA%\ReShade VR UI Add-on\ReShade-System-Wide-SteamVR-Listener.log`.

### Tip: 32-bit And Vulkan SteamVR Games

This add-on has no 32-bit version, but ReShade on its own works in these games (tested with Half-Life 2 VR, which is 32-bit and uses Vulkan).

- Install ReShade for the game as usual. This add-on is not needed.
- Effects show in the headset.
- Press the controller's menu button to open the SteamVR dashboard, then click the ReShade icon for ReShade's menu. This add-on's page is not there.
- If the menu is zoomed in: close the game, open `ReShadeVR.ini` next to the game's `.exe` (the headset menu; `ReShade.ini` is the monitor menu), and under `[STYLE]` set `FontScale=1.000000` and `FontSize=12.000000`.

## UEVR (Unreal Games)

- If ReShade is installed in the game folder (a normal desktop install), turn it off first. Either run ReShade Setup, pick the game and choose **Uninstall**, or rename ReShade's `dxgi.dll` next to the game's `.exe` (for example to `dxgi.dll.off`; rename it back for flat play). UEVR needs ReShade only as an OpenXR layer.
- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- In ReShade Setup, pick the game's `...-Win64-Shipping.exe`, choose **DirectX 10/11/12**, and enable **OpenXR**.
- Close games and SteamVR, then right-click `Install-OpenXR.cmd` and choose **Run as administrator** (once for all OpenXR games).
- Start the game, then inject with UEVR set to **OpenXR** (not OpenVR).
- The panel appears in the headset. `ReShade effect runtime unavailable` may show for a moment, then effects load.
- Switch effects on in the panel. A new game starts with none enabled.

## Update Or Uninstall

- Update OpenXR: close games and SteamVR, extract the new normal release, then run `Install-OpenXR.cmd` as administrator.
- For SteamVR games set up by copying, also replace `ReShade-SteamVR-UI.addon64` in each game folder with the new one.
- Update the system-wide listener: extract its new separate ZIP, then run `Install-System-Wide-SteamVR-Listener.cmd` as administrator.
- Uninstall OpenXR: run `Uninstall-OpenXR.cmd` as administrator.
- Uninstall the listener: run `Uninstall-System-Wide-SteamVR-Listener.cmd` from the listener ZIP as administrator. Removing one does not remove the other.
- For SteamVR games set up by copying, delete `ReShade-SteamVR-UI.addon64` from each game folder.
- If no effects appear, check `ReShade.log` next to the game's `.exe`.

## Where It Installs And Why Admin Rights Are Needed

- OpenXR files go to `C:\Program Files\ReShade VR UI Add-on`, with the layer file under `openxr-layers`. Admin rights are needed to write there and register the layer for all users under `HKEY_LOCAL_MACHINE\Software\Khronos\OpenXR\1\ApiLayers\Implicit`.
- The separate listener installs `ReShade-System-Wide-SteamVR-Listener.exe` and the SteamVR add-on in the same program directory. It starts with Windows for your account through `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`.
- Settings and logs are kept in `%LOCALAPPDATA%\ReShade VR UI Add-on`. Uninstalling does not remove them.
- None of the installers change game folders. The only file placed in a game folder is the SteamVR add-on you copy there yourself.
