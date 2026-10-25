# Limitations

## All Games

- Windows and 64-bit games only.
- DirectX 11, DirectX 12 and OpenGL for OpenXR games; DirectX 11 and 12 for
  SteamVR (OpenVR) games. Vulkan games are not supported; ReShade alone works
  there (see [INSTALL-Vulkan-32bit.md](INSTALL-Vulkan-32bit.md)).
- Needs official ReShade with full add-on support. Without it the add-on does
  nothing. OpenXR games and UEVR: set ReShade up with the **OpenXR layer** ticked.
  SteamVR games: leave the **OpenXR layer** unticked.
- Installing, updating and uninstalling the OpenXR add-on need an
  administrator window. The SteamVR add-on is just copied into the game
  folder.
- Games or mods that bring their own ReShade (for example the D2R-PCVR mod for
  Diablo II: Resurrected): the add-on notices this by itself. Its panel then
  controls the game's ReShade, and it adds no headset effects of its own. Tested
  in D2R-PCVR. See
  [MOD_OWN_RESHADE_CONFLICT.md](MOD_OWN_RESHADE_CONFLICT.md).
- Controllers: Oculus Touch style controllers work best. Other controllers use
  a basic fallback with fewer controls.
- The **UI starts open** setting applies from the next game start.
- The FPS counter measures how often the game hands a frame to the headset.
  It does not show frames the headset software fills in by itself
  (reprojection, motion smoothing).
- The FPS counter does not notice when the headset software runs the game at
  half rate. Pick **FPS target** yourself so the graph colours match.
- The **CPU** line is the frame time minus the time the game spent waiting
  for the headset software. It is not graphics card (GPU) time. If the game
  waits for the graphics card inside its own work, that wait counts as CPU.
  Low CPU with missed frames usually means the graphics card is the limit.

## OpenGL Games

- Tested with SOMA-VR. Other OpenGL OpenXR games should work the same way
  but have not been tried.
- ReShade must run only as the OpenXR layer. A ReShade `opengl32.dll` in the
  game folder stops effects reaching the headset.
- ReShade's built-in **Generic Depth** helper is expensive in OpenGL (about
  30 FPS in SOMA-VR). Untick **Depth buffer (off = faster in OpenGL)** in the
  panel and restart the game to save that; shaders that need depth then get
  none.
- The **Depth buffer** and **Performance mode** tick boxes take effect only
  after a game restart, because ReShade reads them once at start.
- With no effects switched on, ReShade itself still costs a few FPS in OpenGL
  (about 5 in SOMA-VR). This is ReShade, not the add-on.
- ReShade also runs on the game's desktop window, so its own menu (**Home**)
  works there. While the headset shows effects, the add-on switches ReShade's
  effects off on the monitor window to save performance; they come back if
  the headset effects stop.
- Effects apply to the game's 3D view only. Flat title or menu screens that a
  mod shows as a floating panel get no effects.

## SteamVR (OpenVR) Games

- The add-on's page inside ReShade's own SteamVR dashboard
  (**Add-ons** > **ReShade VR UI Add-on for SteamVR**) shows only the FPS
  line, graph and FPS target; effects and presets are controlled in ReShade's
  own menu. There is no separate panel.
- The FPS line and graph are on that page only; there is no always-on counter.
- The add-on changes three settings for the game: it writes `ReShadeVR.ini`
  (without that file the ReShade icon does not appear in SteamVR; the add-on
  also sets a smaller menu text size there once), turns off ReShade's
  desktop/VR effect sync in `ReShade.ini`, and turns effects off on the
  monitor window while the headset shows them.

## UEVR (Unreal Games)

- UEVR must be set to **OpenXR**, not OpenVR.
- ReShade installed in the game folder is not supported with UEVR. Uninstall
  it there or rename its `dxgi.dll`, and use ReShade only as an OpenXR layer.
- **No depth effects.** Effects that need to know how far away things are
  (for example MXAO ambient occlusion, depth of field, depth fog) get no depth
  information. Colour, sharpening and tone effects work. The panel shows a
  note about this above the effect list.
- Both eyes are processed as one wide picture. Whole-screen effects such as
  vignette or lens effects stretch across both eyes instead of each eye.
  Official ReShade VR behaves the same way.
- ReShade's own VR menu is not shown. Use the add-on's panel instead.
- A new game starts with no effects switched on.
- "Waiting for ReShade..." shows until the game draws its first 3D view.

Not tested yet (may or may not work):

- UEVR's 2D-screen mode and extreme compatibility mode (not handled).
- Headset software other than the OpenXR Simulator and Pimax, such as SteamVR
  or Virtual Desktop.

Not caused by the add-on:

- The game needs a force close at the end, also without ReShade.
- DirectX 12 error messages about UEVR's "Game UI Texture" come from UEVR.
