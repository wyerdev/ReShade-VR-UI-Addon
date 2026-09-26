# UEVR + ReShade VR Research

Date started: 2026-09-24. Status (2026-09-26): option 1 is built and shows
effects in both eyes of Creatures of Ava (D3D12) on the OpenXR Simulator.

This note replaces the conclusion of `UEVR_OPENXR_RESHADE_RESEARCH.md`. That
note is correct about why the official ReShade VR path skips UEVR, but it
missed a supported ReShade feature that works around the problem.

## Short Answer

| Option | Verdict |
| --- | --- |
| 1. One ReShade add-on only | **Very likely possible.** Recommended. Needs a prototype to prove it. |
| 2. UEVR C++ plugin + add-on | Not needed for basic effects. Useful later for depth effects. |
| 3. Custom ReShade build | Not needed. Would not be simpler than option 1. |

The plan: our add-on already sits in the OpenXR chain as its own layer. When
official ReShade refuses the UEVR session, the add-on asks ReShade to create an
effect runtime for UEVR's existing DirectX objects, using ReShade's public
add-on function made for this case. The add-on then applies the effects to the
headset images itself.

## What Changed Compared To The Old Conclusion

1. **ReShade has a public function for objects it did not wrap.**
   `reshade::create_effect_runtime()` (`ReShadeCreateEffectRuntime`) builds a
   full effect runtime from a plain, unwrapped device, command queue and DXGI
   swapchain. ReShade documents it as meant "for when it was not already hooked
   by ReShade". It does not check for ReShade's wrapped (proxy) objects.
2. **The runtime can draw on any texture, not just its swapchain.**
   `effect_runtime::render_effects(cmd_list, rtv, rtv_srgb)` accepts any render
   target view. If the size or format differs from the runtime's own swapchain,
   ReShade creates a matching internal copy automatically ("effect
   permutation"). So the runtime can process UEVR's headset images directly.
3. **Game-folder ReShade blocks the official VR layer entirely.** When ReShade
   is installed in the game folder (for example as `dxgi.dll`), the ReShade
   OpenXR layer cannot load later: ReShade refuses to start a second copy of
   itself in one process. The OpenXR loader then silently skips that layer. So
   in that setup the official VR path never even runs. Our add-on layer still
   loads, because it is a different DLL.
4. **The "wrapped device" marker is stored on the original device.** ReShade
   saves a pointer to its wrapper inside the original Direct3D device. So the
   official Direct3D 11 check would pass for UEVR if the same ReShade copy had
   been loaded when the game started. Direct3D 12 still fails, because ReShade
   also needs its wrapped command queue and UEVR hands over the original one.

## How UEVR Sends Images To The Headset (Verified In Source)

- UEVR hooks the real swapchain `Present`, under any ReShade wrapper. It reads
  the device, the swapchain and, for D3D12, the command queue from the real
  swapchain. These are the original objects, never ReShade's wrappers.
- Unless extreme compatibility mode is on, UEVR copies Unreal's stereo render
  target into OpenXR images. It does not copy the monitor backbuffer. This is
  why desktop ReShade effects never show up in the headset.
- Native stereo: one double-width image holds both eyes. AFR/synced
  sequential: one image per eye, usually one eye per game frame.
- The game HUD goes to a separate quad layer, so effects applied to the eye
  images do not touch the HUD.
- Eye images are created as `B8G8R8A8_UNORM_SRGB` with sampled and
  color-attachment usage, so they can be used as render targets.
- For each copy, UEVR acquires the image, waits for it, copies into it on the
  game's queue (D3D12) or immediate context (D3D11), then releases it.
  `xrEndFrame` follows, inside UEVR's `Present` hook on the render thread.

## Option 1 Design (Built: `openxr_effect_fallback.cpp`)

As built: the fallback starts 10 frames after the session begins if no
official VR runtime exists. The hidden swapchain is the size of all eye views
side by side. At release, the eye views are copied into its back buffer,
`render_effects` runs on that back buffer, the result is copied back, then
`update_and_present_effect_runtime` submits the work. Rendering on the eye
image directly (step 5 below) was replaced by this copy.

1. The add-on (already an OpenXR layer) records the D3D11/D3D12 graphics
   binding in `xrCreateSession`.
2. If no official windowless VR runtime appears for that session, the add-on
   turns on its fallback. This check uses the public add-on API only.
3. It creates a small hidden DXGI composition swapchain on the same device
   (D3D11) or queue (D3D12). It then calls `reshade::create_effect_runtime`
   with the raw device, queue and that swapchain, and a config path such as
   `ReShadeVR.ini` in ReShade's base path.
4. In `xrCreateSwapchain` it adds `TRANSFER_SRC | TRANSFER_DST |
   MUTABLE_FORMAT` to color swapchains, as official ReShade does. This makes
   unorm and sRGB views possible.
5. Per frame it calls `update_and_present_effect_runtime` to keep effect
   loading, timers and input running. Then it calls `render_effects` on the
   eye image.
6. Preferred timing is the add-on's own `xrReleaseSwapchainImage`, before the
   image is passed on. The image is still owned by the app at that point, and
   the work is queued after UEVR's copy on the same queue or context.
   `xrEndFrame` is the fallback (official ReShade uses `xrEndFrame`).
7. The existing VR UI keeps controlling that runtime. With a composition
   swapchain, the runtime already counts as "windowless" in the current code.

This works in both ways ReShade can be present in a UEVR process:

- **A. ReShade in the game folder** (normal desktop install, loaded at game
  start). The add-on registers with that ReShade copy. The official VR layer is
  blocked (point 3 above).
- **B. ReShade only as an OpenXR layer**, loaded late when UEVR starts OpenXR.
  Nothing is wrapped. Official ReShade skips the session and logs "proxy
  device" warnings. The add-on fallback takes over.

## Risks And Open Questions

Answered by the prototype (2026-09-26):

- Installed ReShade 6.8.0 exports `ReShadeCreateEffectRuntime` and it works on
  UEVR's raw D3D12 device and queue.
- Release-time processing reaches the Simulator's headset image.
- With the game's `-d3ddebug` switch, the D3D12 error checker reports no
  errors from ReShade's commands. It does report UEVR's own state errors on
  its "Game UI Texture" (not ours).
- The game needs a force close at the end of runs even without ReShade, so
  that is not caused by the add-on.
- D3D11 works: Jedi Fallen Order (2026-09-26) loads 89 techniques on the
  game's D3D11 device, one runtime per session.

Still open:

- **Dummy swapchain in setup A with D3D11:** ReShade's factory hook wraps new
  swapchains for devices that carry its marker. The factory must be taken
  from the device (`IDXGIDevice -> adapter -> parent`) so the swapchain stays
  unwrapped. Otherwise `create_effect_runtime` fails its device check.
- **One render per frame:** views of one image are copied side by side and
  rendered once. Separate per-eye images (Jedi Survivor, 2026-09-26) are held
  until every eye is released, then rendered together once. Before this, the
  one-pixel width difference between eyes rebuilt the runtime at every eye, so
  no effects ever loaded.
- **Side-by-side processing:** effects on a double-width image treat both eyes
  as one picture. Official ReShade VR behaves the same way.
- **Timing:** works on the Simulator. SteamVR and Virtual Desktop untested.
- **No depth:** a created runtime has no depth buffer detection. Depth effects
  need extra work (see option 2).
- **Desktop and VR preset sync:** ReShade's built-in "Effect Runtime Sync"
  add-on may mirror presets between the desktop and VR runtimes in setup A.
  Decide whether to keep that.
- **Not covered:** UEVR 2D-screen mode (the game is shown on a quad, with no
  projection layer) and extreme compatibility mode.
- **UEVR with OpenVR, D3D11, and ReShade in the game folder** may already work
  with official ReShade, because its OpenVR hook uses the device marker. This
  is not verified.

## Option 2: UEVR Plugin (Optional)

- The plugin API gives the real device, swapchain and queue
  (`API::param()->renderer`). It also gives `on_present`, render-thread Slate
  callbacks, and access to `SceneDepthZ` through the render target pool.
- Plugin `on_present` runs after UEVR's VR `on_present`, which has already
  copied and submitted the frame. So a plugin cannot process the image before
  the copy without its own hooks. The OpenXR layer is the better place to do
  the processing.
- Value: providing UEVR's scene depth to effects, and using the real game
  swapchain instead of a dummy one. It can be a small separate DLL that talks
  to the add-on.

## Option 3: Custom ReShade (Not Needed)

- A small patch could let ReShade's OpenXR `xrCreateSession` build wrappers
  around raw objects (as `ReShadeCreateEffectRuntime` does). That would give
  the full official VR runtime, including ReShade's VR GUI.
- It does not fix setup A, because the duplicate-copy rule still blocks the
  layer. It also adds a custom ReShade build to ship. Better proposed upstream
  as a pull request, if at all.

## Test Tooling Facts

- The UEVR frontend (separate `uevr-frontend` repo, not in `.deps`) supports
  `UEVRInjector.exe --attach=<exe name>`. It waits for the process, then
  injects `openxr_loader.dll` or `openvr_api.dll` (per its saved setting)
  followed by `UEVRBackend.dll`. It still opens its window.
- The UEVR backend treats a pre-loaded `openxr_loader.dll` as "use OpenXR". So
  a simple test injector can load UEVR's `openxr_loader.dll` and then
  `UEVRBackend.dll`.
- The OpenXR Simulator is selected with `XR_RUNTIME_JSON` in the game process
  environment. It can capture the headset image on request
  (`tools/test_openxr_simulator.ps1`).
- `.deps/UEVR` builds (verified 2026-09-24, VS 2022 Build Tools, CMake 4.4):
  - UESDK is fetched over HTTPS (local override of the SSH URL in
    `.deps/UEVR/.git/config`); SSH has no GitHub host key here.
  - `cmake -S .deps/UEVR -B .deps/UEVR/build -G "Visual Studio 17 2022" -A x64`
    then `cmake --build .deps/UEVR/build --config Release --target uevr`.
  - Output: `.deps/UEVR/build/bin/uevr/UEVRBackend.dll`.
  - The user's separate custom UEVR copy is read-only reference only. It pins
    the same glm, imgui and spdlog commits, and UESDK one update older.
  - No injector is built by this repo; the nightly `UEVRInjector.exe` in the
    UEVR test folder is used instead.
- UEVR ships OpenXR loader 1.0.22. It reads the simulator bundle's manifest
  (`"library_path": "openxr_simulator.dll"`, no folder) as a system-path name
  and fails with -51 (runtime unavailable). A manifest with the full DLL path
  works.
- The injector's OpenXR/OpenVR choice is saved per install folder and defaults
  to OpenVR. UEVR follows whichever loader the injector loaded.
- Starting the game executable directly with `SteamAppId` set skips Steam's
  relaunch (verified: the started process stayed the game process).
- UEVR logs to `%APPDATA%\UnrealVRMod\<game exe name>\log.txt` and keeps
  appending; the test script deletes it before each run. Early
  "Failed to initialize Framework on DirectX 12" lines are retries, not a
  failure.
- The simulator MCP server (`.vscode/mcp.json`) needs Python `mcp` 1.x;
  version 2 removed the functions it uses (`Server.list_tools`).
- UEVR plugins load from `%APPDATA%\UnrealVRMod\<game>\plugins` and
  `%APPDATA%\UnrealVRMod\UEVR\plugins` (all games). Only option 2 needs a DLL
  there; option 1 arrives through the registered OpenXR layers. The Ava
  folder already holds the user's `dump.dll` and `uevr_mcp.dll`, which also
  loaded in the baseline run. `uevr_mcp.dll` serves `http://localhost:8899/api`
  (verified: status, VR status, render details, screenshot). Its VS Code
  bridge (`UevrMcpServer.exe`) is not configured here; the HTTP API is used
  directly.

## Source Evidence

- Public API: `.deps/reshade/include/reshade.hpp` (`create_effect_runtime`,
  `update_and_present_effect_runtime`). Implementation:
  `.deps/reshade/source/addon.cpp`.
- Any-texture rendering and permutations: `.deps/reshade/source/runtime.cpp`
  (`runtime::render_effects`, `on_present`).
- Proxy checks: `.deps/reshade/source/openxr/openxr_hooks_swapchain.cpp`.
  Marker on the original device: `.deps/reshade/source/d3d11/d3d11_device.cpp`
  and `.deps/reshade/source/d3d12/d3d12_device.cpp`.
- Duplicate-copy refusal and base path: `.deps/reshade/source/dll_main.cpp`.
  Loader skips a layer that fails to load:
  `.deps/reshade/deps/openxr/src/loader/api_layer_interface.cpp`.
- Swapchain wrapping rules: `.deps/reshade/source/dxgi/dxgi.cpp`
  (`query_device`).
- UEVR frame path: `.deps/UEVR/src/mods/vr/D3D11Component.cpp`,
  `.deps/UEVR/src/mods/vr/D3D12Component.cpp` (`OpenXR::copy`),
  `.deps/UEVR/src/hooks/D3D12Hook.cpp`, `.deps/UEVR/src/Framework.cpp`,
  `.deps/UEVR/src/Mods.cpp`.
- Plugin API: `.deps/UEVR/include/uevr/API.h`,
  `.deps/UEVR/src/mods/PluginLoader.cpp`. Runtime choice:
  `.deps/UEVR/src/mods/VR.cpp`.
