# Limitations

## All Games

- Windows only. 32-bit OpenXR games: only DirectX 11 is tested (BioShock Remastered VR).
- DirectX 11, DirectX 12 and OpenGL for OpenXR games; DirectX 11 and 12 for
  SteamVR (OpenVR) games. Vulkan games are not supported; ReShade alone works
  there (see [INSTALL-Vulkan.md](INSTALL-Vulkan.md)).
- Needs official ReShade with full add-on support. Without it the add-on does
  nothing. OpenXR games and UEVR: set ReShade up with the **OpenXR** layer **ticked**.
  SteamVR games: leave the **OpenXR** layer **unticked**.
- Installing, updating and uninstalling the OpenXR add-on need an
  administrator window.
- Games or mods that bring their own ReShade (for example the D2R-PCVR mod for
  Diablo II: Resurrected): the add-on detects this by itself. Its menu then
  controls the game's ReShade, and it adds no headset effects of its own. Tested
  in D2R-PCVR. See
  [MOD_OWN_RESHADE_CONFLICT.md](MOD_OWN_RESHADE_CONFLICT.md).
- Controllers: Oculus Touch style controllers work best. Other controllers get
  fewer controls.
- The head gesture for switching presets (**Next preset** on **Gestures**)
  works only in OpenXR games, not yet in SteamVR (OpenVR) games, and needs
  Oculus Touch style controllers. The game also receives the button press.
  The **L3+R3** choice does nothing while **Controller buttons** is also
  **L3+R3**; pick **Hold L3+R3** there to use both.
- **Walk by swinging arms** works only in OpenXR games that walk with the left
  thumbstick, not in SteamVR (OpenVR) games. **Run** works only in games that
  sprint with a left thumbstick click (`L3`) or a held A, B, X or Y button.
- **Open when the game starts** takes effect the next time you start the game.
- The FPS counter counts the frames the game sends to the headset.
  It does not count frames the headset software makes up by itself
  (reprojection, motion smoothing).
- The FPS counter does not notice when the headset software runs the game at
  half speed. Set **FPS graph target** yourself so
  the graph colours are right.
- The **CPU** line is the frame time minus the time the game spent waiting
  for the headset software. It is not graphics card (GPU) time. If the game
  waits for the graphics card inside its own work, that wait counts as CPU.
  Low CPU with missed frames usually means the graphics card is the limit.

## OpenGL Games

- Tested with SOMA-VR. Other OpenGL OpenXR games should work the same way
  but have not been tried.
- ReShade must run only as the OpenXR layer. A ReShade `opengl32.dll` in the
  game folder stops effects reaching the headset.
- ReShade's built-in **Generic Depth** add-on is slow in OpenGL (it costs
  about 30 FPS in SOMA-VR). Switch off **Enable depth-based effects** in the
  menu and restart the game to get those FPS back; effects that need depth
  then stop working.
- **Enable depth-based effects** and **Faster shaders** only take effect
  after you restart the game, because ReShade reads them once at start.
- With no effects switched on, ReShade itself still costs a few FPS in OpenGL
  (about 5 in SOMA-VR). This is ReShade, not the add-on.
- ReShade also runs in the game's window on the monitor, so its own menu
  (**Home**) works there. While the headset shows effects, the add-on turns
  ReShade's effects off on the monitor to save FPS; they come back if the
  headset effects stop.
- Effects only apply to the game's 3D view. Flat title or menu screens that a
  mod shows as a floating panel get no effects.

## UEVR

- UEVR must be set to **OpenXR**, not OpenVR.
- ReShade installed in the game folder does not work with UEVR. Uninstall
  it there or rename its `dxgi.dll`, and only use ReShade as an OpenXR layer.
- **No depth effects.** Effects that need to know how far away things are
  (for example MXAO ambient occlusion, depth of field, depth fog) get no depth
  information. Colour, sharpening and tone effects work. The menu shows a
  note about this above the effect list.
- Both eyes are handled as one wide picture. Whole-screen effects such as
  vignette or lens effects stretch across both eyes instead of each eye.
  Official ReShade VR does the same.
- ReShade's own VR menu does not show. Use the add-on's menu instead.
- A new game starts with all effects off.
- "Waiting for ReShade..." shows until the game draws its first 3D view.

Not tested yet (may or may not work):

- UEVR's 2D-screen mode and extreme compatibility mode.
- Headset software other than the OpenXR Simulator and Pimax, such as SteamVR
  or Virtual Desktop.

Not caused by the add-on:

- The game has to be force-closed when you quit, even without ReShade.
- DirectX 12 error messages about UEVR's "Game UI Texture" come from UEVR.
