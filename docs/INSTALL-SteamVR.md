# ReShade In SteamVR Games

**Download:** [`ReShadeVR.ini`](https://github.com/wyerdev/ReShade-VR-UI-Addon/releases/latest/download/ReShadeVR.ini) (optional: smaller menu text).

SteamVR games don't get the add-on's own headset menu (that is for OpenXR games). Instead, you use ReShade's own menu in the SteamVR dashboard: click the ReShade icon with your controller. **ReShade adds this icon by itself.**

![The ReShade icon in the SteamVR dashboard, with ReShade's menu open above it](images/steamvr-dashboard-reshade-icon.png)

## Install

1. Install ReShade and leave the **OpenXR** layer **unticked**.
2. Optional, smaller menu text: copy `ReShadeVR.ini` next to the game's `.exe`. Already there? See [Troubleshooting](#troubleshooting).
3. In the SteamVR dashboard, click the ReShade icon with your controller.

<details><summary><b>Step by step</b></summary>

1. Download ReShade from [reshade.me](https://reshade.me/).
2. Run ReShade Setup and pick the game's `.exe`. Choose the graphics type the game uses (DirectX or Vulkan). Leave the **OpenXR** layer **unticked**.
3. Optional, smaller menu text: copy the downloaded `ReShadeVR.ini` next to the game's `.exe`. Already there? See [Troubleshooting](#troubleshooting).
4. Start the game. In the SteamVR dashboard, click the ReShade icon with your controller.

</details>

## Help

- **Find the game folder:** in Steam, right-click the game, choose **Manage**, then **Browse local files**. The `.exe` may be in a `Binaries\Win64` folder inside it. Copy files into the folder that has the `.exe`.
- **See file endings like `.exe`:** in File Explorer, open **View** and tick **File name extensions** (Windows 11: **View**, **Show**, **File name extensions**).

## Troubleshooting

- No ReShade icon in SteamVR: check that ReShade is installed for the game.
- You installed ReShade for this game before with the **OpenXR** layer **ticked**: open ReShade Setup, pick the game, choose **Uninstall**, then install again with the **OpenXR** layer **unticked**.
- Menu looks zoomed in: close the game. Put [`ReShadeVR.ini`](https://github.com/wyerdev/ReShade-VR-UI-Addon/releases/latest/download/ReShadeVR.ini) next to the game's `.exe`. Already there? Open it with Notepad and set these lines under `[STYLE]`:

  ```ini
  FontScale=1.000000
  FontSize=12.000000
  ```

- Anything else: close the game and delete `ReShade.log` next to the game's `.exe`. Start the game until the problem shows, close it, then give the new `ReShade.log` to an AI assistant (for example ChatGPT or Copilot) and ask what is wrong.
