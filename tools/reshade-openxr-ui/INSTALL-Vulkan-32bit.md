# ReShade In Vulkan And 32-bit VR Games

**ReShade already works in these games on its own. You don't need the add-on.**

## Install

1. For each game, install ReShade. **Tick** the **OpenXR** layer only for OpenXR games.
2. SteamVR games: put [`ReShadeVR.ini`](https://github.com/wyerdev/ReShade-VR-UI-Addon/blob/main/tools/reshade-openxr-ui/ReShadeVR.ini) next to the game's `.exe`.
3. Open the menu with the controller's menu button, then click the ReShade icon.

![The ReShade icon in the SteamVR dashboard, with ReShade's menu open above it](images/steamvr-dashboard-reshade-icon.png)

<details><summary><b>Step by step</b></summary>

**In SteamVR games, the ReShade icon only appears if the game has a `ReShadeVR.ini` file next to its `.exe`.**

1. Download ReShade from [reshade.me](https://reshade.me/).
2. Run ReShade Setup and pick the game's `.exe`. Choose the graphics type the game uses (Vulkan or DirectX). **Tick** the **OpenXR** layer only for OpenXR games.
3. SteamVR games: [download `ReShadeVR.ini`](https://github.com/wyerdev/ReShade-VR-UI-Addon/blob/main/tools/reshade-openxr-ui/ReShadeVR.ini) (click the download button on that page) and put it in the game folder, next to the game's `.exe`. If the game already has a `ReShadeVR.ini`, keep that one.
4. SteamVR games: start the game. In the headset, press the controller's menu button and click the ReShade icon.

</details>

## Tested

- Half-Life 2 VR (32-bit, Vulkan, SteamVR): effects and ReShade's menu work in the headset.

## Help

- **Find the game folder:** in Steam, right-click the game, choose **Manage**, then **Browse local files**. Put files into the folder that has the `.exe`.
- **See file endings like `.exe`:** in File Explorer, open **View** and tick **File name extensions** (Windows 11: **View**, **Show**, **File name extensions**).
- **Make `ReShadeVR.ini` yourself:** turn on file endings (see above). In the game folder, right-click an empty spot, choose **New**, then **Text Document**, and name it `ReShadeVR.ini`. Open it and paste these lines:

  ```ini
  [STYLE]
  FontScale=1.000000
  FontSize=12.000000
  ```

## Troubleshooting

- No ReShade icon in SteamVR: check that `ReShadeVR.ini` is next to the game's `.exe`. If it is, start the game before SteamVR.
- You installed ReShade for this SteamVR game before with the **OpenXR** layer **ticked**: open ReShade Setup, pick the game, choose **Uninstall**, then install again with the **OpenXR** layer **unticked**.
- Menu looks zoomed in (you kept the game's own `ReShadeVR.ini`): add the lines from [Help](#help) to it.
- Anything else: close the game and delete `ReShade.log` next to the game's `.exe`. Start the game until the problem shows, close it, then give the new `ReShade.log` to an AI assistant (for example ChatGPT or Copilot) and ask what is wrong.
