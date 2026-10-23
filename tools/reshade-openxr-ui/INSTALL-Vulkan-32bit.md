# ReShade In Vulkan And 32-bit VR Games

**ReShade already works in these games on its own. You don't need the add-on.**

In SteamVR games, ReShade has its own menu: press the controller's menu button and click the ReShade icon in the SteamVR dashboard. **The icon only appears if the game has a `ReShadeVR.ini` file next to its `.exe`.** Step 3 below shows how to get it.

**[PICTURE: the ReShade icon in the SteamVR dashboard]**

Something not working? See [Troubleshooting](#troubleshooting) at the end.

## Set Up ReShade

1. Install ReShade from [reshade.me](https://reshade.me/).
2. In ReShade Setup, pick the game's `.exe` and the graphics type it uses (Vulkan or DirectX). Tick **OpenXR** only for OpenXR games; leave it unticked for SteamVR games.
3. SteamVR games: put a `ReShadeVR.ini` next to the game's `.exe`. This makes the ReShade icon appear. [Download it here](https://github.com/wyerdev/ReShade-VR-UI-Addon/blob/main/tools/reshade-openxr-ui/ReShadeVR.ini) (click the download button), or make a text file named `ReShadeVR.ini` and paste these lines into it:

   ```ini
   [STYLE]
   FontScale=1.000000
   FontSize=12.000000
   ```

   If the game already has a `ReShadeVR.ini`, keep that one.
4. SteamVR games: start the game. In the headset, press the controller's menu button and click the ReShade icon.

The lines in the file also set a smaller menu text size, so the menu isn't zoomed in. If you kept the game's own `ReShadeVR.ini` and the menu looks zoomed in, add these lines to it.

## Tested

- Half-Life 2 VR (32-bit, Vulkan, SteamVR): effects and ReShade's menu work in the headset.

## Troubleshooting

- No ReShade icon in SteamVR: check that `ReShadeVR.ini` is next to the game's `.exe`. If it is, start the game before SteamVR.
- You installed ReShade for this SteamVR game before with **OpenXR** ticked: open ReShade Setup, pick the game, choose **Uninstall**, then install again with **OpenXR** unticked.
- Anything else: close the game and delete `ReShade.log` next to the game's `.exe`, so the new log is short. Start the game until the problem shows, close it, then give the new `ReShade.log` to an AI assistant (for example ChatGPT or Copilot) and ask what is wrong.
