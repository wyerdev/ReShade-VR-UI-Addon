# Install ReShade VR UI Add-on For OpenXR Games And UEVR

For 64-bit OpenXR games that use DirectX 11, DirectX 12 or OpenGL, and for Unreal games played in VR with UEVR. Vulkan games are not supported.

## Install

- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- If you installed ReShade for this game before, open ReShade Setup, pick the game and choose **Uninstall** first.
- In ReShade Setup, pick the game's `.exe` (for UEVR games, the one ending in `-Win64-Shipping.exe`), choose **DirectX 10/11/12** (or **OpenGL** if the game uses it), and tick **OpenXR**.
- Close games and SteamVR, then right-click `Install-OpenXR.cmd` and choose **Run as administrator**. You only do this once; it works for all your OpenXR games.
- Start the game. For UEVR, inject with UEVR set to **OpenXR** (not OpenVR).
- The add-on panel appears in the headset. `L3 + R3` shows or hides it. `Waiting for ReShade...` may show for a moment, then effects load.
- Switch effects on in the panel. A new game starts with none enabled.

## OpenGL Games: Depth Buffer And FPS

OpenGL games (for example SOMA-VR) install the same way. Two things are different:

- Effects appear once the game shows its normal 3D view. A flat title screen may show `Waiting for ReShade...` until then.
- ReShade's depth buffer helper (**Generic Depth**) costs much more FPS in OpenGL; in SOMA-VR about 30 FPS. It lets shaders know how far away things are.

For more FPS, untick **Depth buffer (off = faster in OpenGL)** in the headset panel, then restart the game. Shaders that need depth (for example MXAO, depth of field, depth fog) then stop working; the game itself is not affected. Tick it again and restart to get them back. The setting is saved per game.

## Update Or Uninstall

- Update: close games and SteamVR, extract the new ZIP, then run `Install-OpenXR.cmd` as administrator.
- Uninstall: run `Uninstall-OpenXR.cmd` as administrator.
- If no effects appear, check `ReShade.log` next to the game's `.exe`.

## Where It Installs And Why Admin Rights Are Needed

- Files go to `C:\Program Files\ReShade VR UI Add-on`, with the layer file under `openxr-layers`. Admin rights are needed to write there and register the layer for all users under `HKEY_LOCAL_MACHINE\Software\Khronos\OpenXR\1\ApiLayers\Implicit`.
- Settings and logs are kept in `%LOCALAPPDATA%\ReShade VR UI Add-on`. Uninstalling does not remove them.
- The installer does not change game folders.
