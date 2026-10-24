# Install ReShade VR UI Add-on For OpenXR Games And UEVR

Adds a menu in your headset to control ReShade's effects. Install it once; it works in all your OpenXR games.

For 64-bit OpenXR games with DirectX 11, DirectX 12 or OpenGL, and for Unreal games played in VR with UEVR.

**Download:** [newest version](https://github.com/wyerdev/ReShade-VR-UI-Addon/releases/latest) (the file starting with `ReShade-VR-UI-Addon-OpenXR`).

Stuck? See [Help](#help) and [Troubleshooting](#troubleshooting) at the end.

## Install

1. Download ReShade from [reshade.me](https://reshade.me/). Choose the version **with full add-on support**.
2. Run ReShade Setup and pick the game's `.exe`. For UEVR games, pick the one ending in `-Win64-Shipping.exe`.
3. Choose **DirectX 10/11/12** (or **OpenGL**) and tick the **OpenXR layer**.
4. Close all games and SteamVR.
5. Extract the add-on ZIP. Double-click `Install-OpenXR.cmd`. When Windows asks for permission, click **Yes**.
6. Start the game. For UEVR, set UEVR to **OpenXR** before you inject.
7. In the headset, press both thumbsticks in (`L3 + R3`) or the `Insert` key. The menu opens. Turn effects on there.

UEVR uses the same buttons for its own menu. Pick other buttons in the add-on menu's **Open UI** row.

## Update Or Uninstall

- Update: close games and SteamVR, extract the new ZIP, then run `Install-OpenXR.cmd` and click **Yes** when Windows asks for permission.
- Uninstall: run `Uninstall-OpenXR.cmd` and click **Yes** when Windows asks for permission.

## OpenGL Games: More FPS

In OpenGL games (for example SOMA-VR), ReShade's depth helper costs a lot of FPS (about 30 FPS in SOMA-VR).

For more FPS: untick **Depth buffer (off = faster in OpenGL)** in the menu and restart the game. Effects that need depth (for example MXAO, depth of field, fog) then stop working. Tick it again to get them back. The setting is saved per game.

## Help

- **Extract a ZIP:** right-click it, choose **Extract All**, then **Extract**.
- **Find the game folder:** in Steam, right-click the game, choose **Manage**, then **Browse local files**. In ReShade Setup, click **Browse** and go to that folder. The `.exe` may be in a `Binaries\Win64` folder inside it.
- **See file endings like `.exe`:** in File Explorer, open **View** and tick **File name extensions** (Windows 11: **View**, **Show**, **File name extensions**).
- **Administrator rights:** the install and uninstall files ask Windows for them by themselves. Click **Yes** in the permission window. If no window shows, right-click the file, choose **Run as administrator**, then click **Yes**.

## Troubleshooting

- `No administrator rights`: nothing was changed. Run the file again and click **Yes** when Windows asks for permission (see [Help](#help)).
- `Waiting for ReShade...`: wait a moment. In OpenGL games it stays until the game shows its normal 3D view.
- No menu or no effects: check that you installed ReShade **with full add-on support** and ticked the **OpenXR layer**. For UEVR, check that UEVR is set to **OpenXR**.
- Still no menu: the game may be a SteamVR game. Open ReShade Setup, pick the game, choose **Uninstall**, then follow the SteamVR install steps. The OpenXR add-on can stay installed.
- Still no effects: an old ReShade copy in the game folder can block them. Open ReShade Setup, pick the game, choose **Uninstall**, then install again (steps 2 and 3).
- Anything else: close the game and delete `ReShade.log` next to the game's `.exe`. Start the game until the problem shows, close it, then give the new `ReShade.log` to an AI assistant (for example ChatGPT or Copilot) and ask what is wrong.

## Where It Installs

- Files go to `C:\Program Files\ReShade VR UI Add-on`. Admin rights are needed to write there and to switch the add-on on for all OpenXR games (registry key `HKEY_LOCAL_MACHINE\Software\Khronos\OpenXR\1\ApiLayers\Implicit`).
- Admin rights also make it work in games that run as administrator, such as SOMA with the SOMA-VR mod.
- Settings and logs are in `%LOCALAPPDATA%\ReShade VR UI Add-on`. Uninstalling keeps them.
- The installer does not change game folders.
