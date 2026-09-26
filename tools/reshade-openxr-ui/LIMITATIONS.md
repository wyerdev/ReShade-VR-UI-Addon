# Limitations

## All Games

- Windows and 64-bit games only.
- DirectX 11 and 12 only. Vulkan and OpenGL games are not supported.
- Needs official ReShade with full add-on support, set up for the game with
  **OpenXR** enabled. Without it the add-on does nothing.
- Installing, updating and uninstalling need an administrator window.
- Controllers: Oculus Touch style controllers work best. Other controllers use
  a basic fallback with fewer controls.
- The **UI starts open** setting applies from the next game start.

## SteamVR (OpenVR) Games

- The controls are a page inside ReShade's own SteamVR dashboard
  (**Add-ons** > **ReShade VR UI Add-on for SteamVR**), not a separate panel.
- Only games in ReShade Setup's game list are picked up, and only while the
  add-on's loader is running (it starts when you sign in to Windows).
- The add-on changes two settings in the game's ReShade files: it turns off
  ReShade's desktop/VR effect sync in `ReShade.ini`, and it sets the dashboard
  font size once in `ReShadeVR.ini`.

## UEVR (Unreal Games)

- UEVR must be set to **OpenXR**, not OpenVR.
- **No depth effects.** Effects that need to know how far away things are
  (for example MXAO ambient occlusion, depth of field, depth fog) get no depth
  information. Colour, sharpening and tone effects work.
- Both eyes are processed as one wide picture. Whole-screen effects such as
  vignette or lens effects stretch across both eyes instead of each eye.
  Official ReShade VR behaves the same way.
- ReShade's own VR menu is not shown. Use the add-on's panel instead.
- A new game starts with no effects switched on.
- "ReShade effect runtime unavailable" can show for a moment at start.

Not tested yet (may or may not work):

- ReShade also installed in the game folder at the same time.
- UEVR's 2D-screen mode and extreme compatibility mode (not handled).
- Headset software other than the OpenXR Simulator and Pimax, such as SteamVR
  or Virtual Desktop.
- Whether desktop and VR presets stay in sync when ReShade is also in the
  game folder.

Not caused by the add-on:

- The game needs a force close at the end, also without ReShade.
- DirectX 12 error messages about UEVR's "Game UI Texture" come from UEVR.
