# ReShade In SteamVR Games

**Download:** [newest version](https://github.com/wyerdev/ReShade-VR-UI-Addon/releases/latest) (the file starting with `ReShade-VR-UI-Addon-SteamVR`).

SteamVR games don't get the add-on's own headset menu (that is for OpenXR games). Instead, you use ReShade's own menu in the SteamVR dashboard: click the ReShade icon with your controller. **The icon only appears if the game has a `ReShadeVR.ini` file next to its `.exe`.**

![The ReShade icon in the SteamVR dashboard, with ReShade's menu open above it](images/steamvr-dashboard-reshade-icon.png)

## Install

For each game, pick one way:

### Way 1: With The Add-on (Automatic)

For 64-bit DirectX 11 and 12 games. Adds extras, like drawing effects only in the headset.

1. Install ReShade **with full add-on support** and leave the **OpenXR** layer **unticked**.
2. Copy `ReShade-SteamVR-UI.addon64` next to the game's `.exe`.
3. In the SteamVR dashboard, click the ReShade icon with your controller.

<details><summary><b>Step by step</b></summary>

1. Download ReShade from [reshade.me](https://reshade.me/). Choose the version **with full add-on support**.
2. Run ReShade Setup and pick the game's `.exe`. Choose **DirectX 10/11/12**. Leave the **OpenXR** layer **unticked**.
3. Extract the ZIP. Copy `ReShade-SteamVR-UI.addon64` into the game folder, next to the game's `.exe`.
4. Start the game. In the SteamVR dashboard, click the ReShade icon with your controller.

Extras you get:

- Effects are drawn only in the headset, not also on the monitor, which can save FPS.
- Preset changes no longer flip back and forth between the monitor and the headset.
- Smaller menu text, so the menu doesn't look zoomed in.

Update: replace the `.addon64` file. Uninstall: delete it. The ReShade icon stays in SteamVR.

</details>

### Way 2: ReShade Only (Manual)

Without the add-on, so no extras.

1. Install ReShade and leave the **OpenXR** layer **unticked**.
2. Copy `ReShadeVR.ini` from the ZIP's `without-addon` folder next to the game's `.exe`.
3. In the SteamVR dashboard, click the ReShade icon with your controller.

<details><summary><b>Step by step</b></summary>

1. Download ReShade from [reshade.me](https://reshade.me/).
2. Run ReShade Setup and pick the game's `.exe`. Choose the graphics type the game uses (DirectX or Vulkan). Leave the **OpenXR** layer **unticked**.
3. Extract the ZIP. Copy `ReShadeVR.ini` from its `without-addon` folder into the game folder, next to the game's `.exe`. If the game already has a `ReShadeVR.ini`, keep that one.
4. Start the game. In the SteamVR dashboard, click the ReShade icon with your controller.

</details>

## Help

- **Extract a ZIP:** right-click it, choose **Extract All**, then **Extract**.
- **Find the game folder:** in Steam, right-click the game, choose **Manage**, then **Browse local files**. The `.exe` may be in a `Binaries\Win64` folder inside it. Copy files into the folder that has the `.exe`.
- **See file endings like `.exe`:** in File Explorer, open **View** and tick **File name extensions** (Windows 11: **View**, **Show**, **File name extensions**).

## Troubleshooting

- No ReShade icon in SteamVR: check that `ReShadeVR.ini` is next to the game's `.exe`. If it is, start the game before SteamVR.
- You installed ReShade for this game before with the **OpenXR** layer **ticked**: open ReShade Setup, pick the game, choose **Uninstall**, then install again with the **OpenXR** layer **unticked**.
- Menu looks zoomed in (Way 2, kept the game's own `ReShadeVR.ini`): add these lines to it:

  ```ini
  [STYLE]
  FontScale=1.000000
  FontSize=12.000000
  ```

- Anything else: close the game and delete `ReShade.log` next to the game's `.exe`. Start the game until the problem shows, close it, then give the new `ReShade.log` to an AI assistant (for example ChatGPT or Copilot) and ask what is wrong.

## What The Add-on Changes

It changes two files for you: `ReShadeVR.ini` gets a smaller menu text size, and `ReShade.ini` gets `SyncEffectRuntimes=0`. These changes stay after you delete the add-on.
