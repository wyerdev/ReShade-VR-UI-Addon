# Install ReShade VR UI Add-on For OpenXR Games And UEVR

For 64-bit OpenXR games that use DirectX 11, DirectX 12 or OpenGL, and for Unreal games played in VR with UEVR. Not for Vulkan or 32-bit games.

**Install the add-on once. It adds a panel in the headset to control ReShade's effects in all your OpenXR games.**

Something not working? See [Troubleshooting](#troubleshooting) at the end.

## Install

1. Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/). Normal ReShade does not load add-ons.
2. In ReShade Setup, pick the game's `.exe` (UEVR games: the one ending in `-Win64-Shipping.exe`), choose **DirectX 10/11/12** (or **OpenGL**), and tick **OpenXR**, so ReShade draws its effects in the headset. Installed ReShade for this game before? Choose **Uninstall** first, then install again: an old ReShade copy in the game folder can stop effects reaching the headset.
3. Close games and SteamVR. Extract the ZIP, then right-click `Install-OpenXR.cmd` and choose **Run as administrator**. This switches the add-on on for all your OpenXR games, so you do it only once.
4. Start the game. For UEVR, inject with UEVR set to **OpenXR** (not OpenVR).
5. The panel appears in the headset. Press both thumbsticks in (`L3 + R3`) to show or hide it. `Waiting for ReShade...` may show for a moment.
6. Switch effects on in the panel. A new game starts with none on.

## OpenGL Games: Depth Buffer And FPS

OpenGL games (for example SOMA-VR) install the same way. Two things are different:

- Effects appear once the game shows its normal 3D view. A flat title screen may show `Waiting for ReShade...` until then.
- ReShade's depth helper (**Generic Depth**) costs much more FPS in OpenGL: about 30 FPS in SOMA-VR. It tells shaders how far away things are.

For more FPS, untick **Depth buffer (off = faster in OpenGL)** in the panel and restart the game. Shaders that need depth (for example MXAO, depth of field, depth fog) then stop working; the game itself is fine. Tick it again and restart to get them back. The setting is saved per game.

## Update Or Uninstall

- Update: close games and SteamVR, extract the new ZIP, then run `Install-OpenXR.cmd` as administrator.
- Uninstall: run `Uninstall-OpenXR.cmd` as administrator.

## Where It Installs And Why It Needs Admin Rights

- Files go to `C:\Program Files\ReShade VR UI Add-on`. Admin rights are needed to write there and to switch the add-on on for all OpenXR games (registry key `HKEY_LOCAL_MACHINE\Software\Khronos\OpenXR\1\ApiLayers\Implicit`).
- Admin rights are also what makes it work in games that run as administrator, such as SOMA with the SOMA-VR mod. Windows ignores add-ons installed only for your account in those games.
- Settings and logs are kept in `%LOCALAPPDATA%\ReShade VR UI Add-on`. Uninstalling keeps them.
- The installer does not change game folders.

## Troubleshooting

- The installer says `No administrator rights`: nothing was changed. Close the window, right-click `Install-OpenXR.cmd` (or `Uninstall-OpenXR.cmd`), choose **Run as administrator**, and click **Yes** when Windows asks.
- No panel or no effects: check that ReShade for this game was installed **with full add-on support** and with **OpenXR** ticked. For UEVR, check that UEVR is set to **OpenXR**.
- Still no effects: an old ReShade copy in the game folder (for example `dxgi.dll` or `opengl32.dll`) blocks them. Open ReShade Setup, pick the game, choose **Uninstall**, then install again as in step 2.
- Anything else: close the game and delete `ReShade.log` next to the game's `.exe`, so the new log is short. Start the game until the problem shows, close it, then give the new `ReShade.log` to an AI assistant (for example ChatGPT or Copilot) and ask what is wrong.
