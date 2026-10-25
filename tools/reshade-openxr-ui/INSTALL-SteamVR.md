# ReShade In SteamVR Games

In SteamVR games, ReShade has its own menu in the SteamVR dashboard: press the controller's menu button and click the ReShade icon. **The icon only appears if the game has a `ReShadeVR.ini` file next to its `.exe`.** This download includes that file. You set up each game separately.

![The ReShade icon in the SteamVR dashboard, with ReShade's menu open above it](https://raw.githubusercontent.com/wyerdev/ReShade-VR-UI-Addon/main/tools/reshade-openxr-ui/images/steamvr-dashboard-reshade-icon.png)

**Download:** [newest version](https://github.com/wyerdev/ReShade-VR-UI-Addon/releases/latest) (the file starting with `ReShade-VR-UI-Addon-SteamVR`).

For each game, pick one way:

- **Way 1: Automatic.** For 64-bit DirectX 11 and 12 games. The add-on creates `ReShadeVR.ini` for you and adds extras.
- **Way 2: Manual.** For any game. You copy `ReShadeVR.ini` yourself.

## Way 1: Automatic (With The Add-on)

1. Download ReShade from [reshade.me](https://reshade.me/). Choose the version **with full add-on support**.
2. Run ReShade Setup and pick the game's `.exe`. Choose **DirectX 10/11/12**. Leave the **OpenXR layer** unticked.
3. Extract the ZIP. Copy `ReShade-SteamVR-UI.addon64` into the game folder, next to the game's `.exe`.
4. Start the game. ReShade's menu opens in the headset by itself.

Extras you get:

- ReShade's menu opens by itself.
- FPS counter: in ReShade's menu, open **Add-ons** and click **ReShade VR UI Add-on for SteamVR**.
- Effects are drawn only in the headset, not also on the monitor, which can save FPS.
- Preset changes no longer flip back and forth between the monitor and the headset.
- Smaller menu text, so the menu doesn't look zoomed in.

Update: replace the `.addon64` file. Uninstall: delete it. The ReShade icon stays in SteamVR.

## Way 2: Manual (ReShade Only)

1. Download ReShade from [reshade.me](https://reshade.me/).
2. Run ReShade Setup and pick the game's `.exe`. Choose the graphics type the game uses (DirectX or Vulkan). Leave the **OpenXR layer** unticked.
3. Extract the ZIP. Copy `ReShadeVR.ini` from its `without-addon` folder into the game folder, next to the game's `.exe`. If the game already has a `ReShadeVR.ini`, keep that one.
4. Start the game. In the headset, press the controller's menu button and click the ReShade icon.

## Help

- **Extract a ZIP:** right-click it, choose **Extract All**, then **Extract**.
- **Find the game folder:** in Steam, right-click the game, choose **Manage**, then **Browse local files**. The `.exe` may be in a `Binaries\Win64` folder inside it. Copy files into the folder that has the `.exe`.
- **See file endings like `.exe`:** in File Explorer, open **View** and tick **File name extensions** (Windows 11: **View**, **Show**, **File name extensions**).

## Troubleshooting

- No ReShade icon in SteamVR: check that `ReShadeVR.ini` is next to the game's `.exe`. If it is, start the game before SteamVR.
- You installed ReShade for this game before with the **OpenXR layer** ticked: open ReShade Setup, pick the game, choose **Uninstall**, then install again with the **OpenXR layer** unticked.
- Menu looks zoomed in (Way 2, kept the game's own `ReShadeVR.ini`): add these lines to it:

  ```ini
  [STYLE]
  FontScale=1.000000
  FontSize=12.000000
  ```

- Anything else: close the game and delete `ReShade.log` next to the game's `.exe`. Start the game until the problem shows, close it, then give the new `ReShade.log` to an AI assistant (for example ChatGPT or Copilot) and ask what is wrong.

## What The Add-on Changes

It changes two files for you: `ReShadeVR.ini` gets a smaller menu text size, and `ReShade.ini` gets `SyncEffectRuntimes=0`. These changes stay after you delete the add-on.
