# Install ReShade VR UI Add-on For SteamVR Games

For 64-bit SteamVR games that use DirectX 11 or DirectX 12. You set up each game on its own.

## Install

- Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/).
- In ReShade Setup, pick the game's `.exe`, choose **DirectX 10/11/12**, and leave **OpenXR** unticked.
- If you installed ReShade for this game before with **OpenXR** ticked, choose **Uninstall** in ReShade Setup first, then install again with it unticked.
- Copy `ReShade-SteamVR-UI.addon64` from this ZIP into the folder with the game's `.exe`.
- Start the game. ReShade's SteamVR dashboard opens after effects load.
- Open **Add-ons** > **ReShade VR UI Add-on for SteamVR** for the controls.

## Update Or Uninstall

- Update: replace `ReShade-SteamVR-UI.addon64` in each game folder with the new one.
- Uninstall: delete `ReShade-SteamVR-UI.addon64` from each game folder.
- If no effects appear, check `ReShade.log` next to the game's `.exe`.

## Tip: 32-bit And Vulkan SteamVR Games

This add-on has no 32-bit or Vulkan version, but ReShade on its own works in these games (tested with Half-Life 2 VR, which is 32-bit and uses Vulkan).

- Install ReShade for the game as usual. This add-on is not needed.
- Effects show in the headset.
- Press the controller's menu button to open the SteamVR dashboard, then click the ReShade icon for ReShade's menu.
- If the menu is zoomed in: close the game, open `ReShadeVR.ini` next to the game's `.exe`, and under `[STYLE]` set `FontScale=1.000000` and `FontSize=12.000000`.
