# ReShade In SteamVR Games

In SteamVR games, ReShade has its own menu: press the controller's menu button and click the ReShade icon in the SteamVR dashboard. **The icon only appears if the game has a `ReShadeVR.ini` file next to its `.exe`.** Set up each game on its own.

**[PICTURE: the ReShade icon in the SteamVR dashboard]**

**There are 2 ways. Pick one per game:**

- **Way 1: Automatic (with the add-on).** For 64-bit DirectX 11 and 12 games. The add-on writes `ReShadeVR.ini` for you, opens ReShade's menu by itself, and adds an FPS counter.
- **Way 2: Manual (ReShade alone).** For any game. You copy `ReShadeVR.ini` from this ZIP yourself.

Something not working? See [Troubleshooting](#troubleshooting) at the end.

## Way 1: Automatic (With The Add-on)

1. Install **ReShade with full add-on support** from [reshade.me](https://reshade.me/). Normal ReShade does not load add-ons.
2. In ReShade Setup, pick the game's `.exe`, choose **DirectX 10/11/12**, and leave **OpenXR** unticked (that option is only for OpenXR games).
3. Copy `ReShade-SteamVR-UI.addon64` from this ZIP next to the game's `.exe`. ReShade loads add-ons from there.
4. Start the game. ReShade's menu opens in the headset by itself once effects have loaded.

What the add-on does, most important first:

- Writes `ReShadeVR.ini`, so the ReShade icon appears.
- Opens ReShade's menu by itself.
- FPS counter with a graph: in ReShade's menu, open **Add-ons** and click **ReShade VR UI Add-on for SteamVR**.
- Stops preset changes bouncing between the monitor and the headset (`SyncEffectRuntimes=0` in `ReShade.ini`).
- Turns effects off on the monitor window, so they are drawn only once. This might save FPS.
- Sets a smaller menu text size in `ReShadeVR.ini`, so the menu isn't zoomed in.

Update: replace the `.addon64` file. Uninstall: delete it. The settings it wrote stay in the game's `ReShade.ini` and `ReShadeVR.ini`, so the icon keeps appearing.

## Way 2: Manual (ReShade Alone)

1. Install ReShade from [reshade.me](https://reshade.me/).
2. In ReShade Setup, pick the game's `.exe` and the graphics type it uses (DirectX or Vulkan), and leave **OpenXR** unticked (that option is only for OpenXR games).
3. Copy `ReShadeVR.ini` from this ZIP's `without-addon` folder next to the game's `.exe`. This makes the ReShade icon appear. If the game already has a `ReShadeVR.ini`, keep that one.
4. Start the game. In the headset, press the controller's menu button and click the ReShade icon.

The file also sets a smaller menu text size (`FontScale=1.000000` and `FontSize=12.000000` under `[STYLE]`), so the menu isn't zoomed in. If you kept the game's own `ReShadeVR.ini` and the menu looks zoomed in, add those lines to it.

You don't get the other extras from Way 1.

## Troubleshooting

- No ReShade icon in SteamVR: check that `ReShadeVR.ini` is next to the game's `.exe`. If it is, start the game before SteamVR.
- You installed ReShade for this game before with **OpenXR** ticked: open ReShade Setup, pick the game, choose **Uninstall**, then install again with **OpenXR** unticked.
- Anything else: close the game and delete `ReShade.log` next to the game's `.exe`, so the new log is short. Start the game until the problem shows, close it, then give the new `ReShade.log` to an AI assistant (for example ChatGPT or Copilot) and ask what is wrong.
