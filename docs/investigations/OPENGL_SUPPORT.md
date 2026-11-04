# OpenGL Support

Goal: ReShade effects and the headset menu for OpenXR games that draw with OpenGL.
Test game: SOMA with the SOMA-VR mod (source in `.deps\SOMA-VR`), run through
`tools/test_soma_vr.ps1`.

## Status

| Part | State |
|------|-------|
| ReShade accepts the OpenGL headset session | Done (2026-09-30), confirmed on the simulator and the headset |
| ReShade effects visible in the headset | Done (2026-09-30), user saw the test invert effect in the headset |
| Headset menu drawn with OpenGL | Done (2026-09-30), user confirmed on the headset: visible, controller clicks work, L3 + R3 hides it |
| No crash after loading a save in a normal start | Fixed (2026-09-30), user confirmed with `Launch-SOMA-VR.cmd` and the installed build |

## Findings

- **Why ReShade skipped SOMA's headset.** ReShade is loaded by its OpenXR layer
  only when the mod starts OpenXR. By then SOMA has already set up its OpenGL
  drawing connection (the "context"). ReShade learns about a context only by
  watching the game switch to it, so it had none on record and logged
  "Skipping OpenXR session because it was created without an OpenGL context
  current".
- **Fix in the add-on.** ReShade's layer sits above ours and checks for a
  context only after our layer returns from session creation. Our layer now
  switches the game's context off and back on (through ReShade's hooked
  `wglMakeCurrent`) during session creation, on the same thread, only when that
  context is current. ReShade then registers it and accepts the session.
  Code: `announce_opengl_context` in `openxr_ui_layer.cpp`.
- **Side effect.** ReShade also starts its normal desktop runtime for SOMA's
  window, as it would with an early-loaded ReShade.
- **Do not put ReShade's `opengl32.dll` in the game folder.** That copy loads
  first, and the layer's copy then refuses to start ("Another ReShade instance
  was already loaded"), so nothing handles the headset.
- **No workaround through our own effect runtime.** ReShade's
  `ReShadeCreateEffectRuntime` has no OpenGL case, unlike D3D11/D3D12 (UEVR).
- **Effects need stereo (projection) layers.** ReShade applies effects only to
  projection layers, and starts its headset runtime (`ReShadeVR.ini`,
  8680x4284 for both eyes) at the first one. SOMA-VR sometimes shows its title
  as a flat quad first; in one headset run the runtime started at launch, in
  another only once the 3D view began. On the simulator the mod only shows a
  flat quad before it stalls, so no runtime appears there.
- **The mod's session thread.** SOMA-VR creates the session, copies each eye
  into the headset images and ends each frame on the game's render thread with
  its context current. The OpenGL menu relies on this.
- **OpenGL menu.** The menu and FPS counter are drawn with Dear ImGui's
  `imgui_impl_opengl3` into OpenGL quad swapchains, in the game's context, only
  when that context is current. The add-on restores framebuffer binding, clear
  colour, colour mask, scissor and sRGB state, and never waits for the GPU
  or flushes. OpenGL is loaded at run time, so the add-on still has no
  `opengl32.dll` import. The UEVR effect fallback stays Direct3D-only.
- **Seated position.** The mod places its title panel relative to the head at
  start. Reset the seated position in the Pimax software before launching,
  otherwise the title can be far below and the view looks black.

## Test Notes

- By default the test uses a repo-owned ReShade folder with only the invert
  test effect. `-UseGameEffects` loads the effects installed in the game
  folder (read only) for both the headset and the monitor window, and starts
  from a copy of the game's preset, like a normal start.
- A normal start (`Launch-SOMA-VR.cmd`) uses the installed add-on in Program
  Files; run `install.bat` after each build to test it that way.
- Frame rate (2026-09-30, headset, user report): with ReShade and the add-on
  some areas fell to 40-50 FPS, also with all effects temporarily disabled
  (from memory) and whether the menu was shown or not. Without ReShade and
  the add-on (`-WithoutLayers`) the same areas fell only to 70-80. Headset
  half-rate mode is off. Direct3D games lose only 1-2 ms with ReShade, so the
  OpenGL path has extra overhead. Suspect: once the add-on hands ReShade the
  game's context, ReShade's OpenGL hooks process every game OpenGL call.
  Next split: `-WithoutReShade` (add-on only) against the full run.
- `-WithoutReShade` result: normal frame rate, but the add-on then stays a
  pass-through layer (no menu, because its controller actions need ReShade).
  Since hiding the menu did not help in the full run either, the cost is in
  ReShade once it tracks the context. First suspect: the built-in Generic
  Depth add-on, which inspects every OpenGL depth binding and draw call. Test
  with `-DisabledReShadeAddons 'Generic Depth'`.
- Generic Depth off result: effects active and 70-80 FPS, close to no
  ReShade. Generic Depth is the main ReShade cost in SOMA-VR; depth-based
  effects need it. Separately, opening the menu cost about 20 FPS at once
  (the pinned FPS counter alone did not): the OpenGL menu called `glFinish`
  every frame, stalling the game until the GPU was idle. Replaced with
  `glFlush` (2026-09-30), then removed entirely (see below).
- Headset recheck (flush build): opening the menu no longer drops the frame
  rate and it does not flicker. Turning all effects off gains 5-10 FPS;
  ReShade with no effects is still up to ~5 FPS below no ReShade.
- Rule 1 follow-up: the D3D11, D3D12 and OpenGL menus no longer wait for the
  GPU or flush. D3D12 uses a ring of four command allocators; when the next
  one is still in use the menu skips that frame (the headset keeps the last
  menu image), and at session end busy D3D12 objects are freed later instead
  of waited for. A temporary "Menu GPU sync"
  test setting showed on the headset: no wait and no flush has no flicker,
  "Wait for GPU" costs about 20 FPS. The setting was removed; the menu never
  waits. D3D12 simulator captures look the same as before these changes (the
  thin text there is the simulator's preview scaling).
- Headset check of the never-waiting D3D12 menu (2026-09-30): user reports
  a successful test in a D3D12 game.
- Panel tick box **Depth buffer (off = faster in OpenGL)** (the SteamVR
  page's copy is hidden for now, see `DEVELOPMENT.md`; ticked by default) adds or removes `Generic Depth` in ReShade's
  `ADDON/DisabledAddons` list, keeping other entries. It sits next to
  **Performance mode (on = faster shaders)**; a line below both says they apply
  after a game restart and turns amber while a change waits for one.
- Test runs use the script's own ReShade settings, not the game's. Every
  run after the no-wait change had Generic Depth off only there; the game's
  own `ReShade.ini` always had it on.
- Crash (2026-09-30, user's own normal start, three times, depth buffer on):
  SOMA closed with a stack overflow (0xC00000FD) right after a save loaded,
  as ReShade's headset runtime loaded the game's effects. Cause: to learn
  default values, the add-on reset and rewrote every shader setting inside
  ReShade's "effects reloaded" callback. ReShade's desktop/headset sync
  copied each write to the monitor runtime and back again, and ReShade's
  echo guard does not work for changes that start inside another add-on's
  callback. Test runs gave the monitor runtime only the invert effect, so
  nothing matched and it never showed. Fix: defaults are captured later, in
  batches of 32, from the menu path (outside ReShade callbacks).
  `-UseGameEffects` now gives the monitor runtime the game's effects too.
  User confirmed: no crash in a normal start with the installed fix.
- While a headset effect runtime exists, the add-on switches off effects on
  the monitor window (`set_effects_state(false)`, which does not sync).
- Shader cost (2026-09-30, user report, normal start, Colourfulness + FakeHDR
  + LevelsPlus): headset CPU time 6-7 ms with shaders off, 10-12 ms on, so
  90 Hz is missed. With Performance mode CPU time rose less but the FPS drops
  stayed, so the graphics card is the limit. Performance mode only speeds up
  shader maths, so ReShade's full-size (8680x4284) copies around the effects
  may be a large share. Unconfirmed; check with only Colourfulness on.
- Idea, not planned: run effects on a half-size copy and apply only the
  colour change to the full image. Possible for D3D11/D3D12 through the
  add-on's own effect runtime; not for OpenGL, where only ReShade's own
  full-size headset runtime can run effects.
- `-Action Run -CaptureFrame 0` checks the session-level result: the add-on
  log line "OpenGL session context re-bound" and no ReShade "Skipping OpenXR
  session" warning. Passed on the simulator.
- On the OpenXR Simulator SOMA-VR submits one frame and then stops, also with
  both layers disabled (`-WithoutLayers`), so simulator captures of SOMA are
  not available yet. The game stays responsive but idle. Giving the SOMA
  window focus did not help. Cause not found.
- `-UseActiveRuntime` needs the headset connected; with it off, the mod fails
  at `xrGetSystem` (form factor unavailable) and creates no session. With it
  on (2026-09-30) the session check passed and the headset effect runtime
  started; the only errors were the expected "native quad skipped" (no OpenGL
  menu yet) and ReShade's SteamVR dashboard message (SteamVR not in use).

## Next

1. An OpenGL simulator probe for automated tests (the simulator supports
   OpenGL sessions and screenshots).
