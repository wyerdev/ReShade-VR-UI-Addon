# 32-bit OpenXR Games (BioShock Remastered VR)

Goal: the OpenXR add-on works in 32-bit OpenXR games without changing the 64-bit add-on.

## The Test Game

- BioShock Remastered (`BioshockHD.exe`, Steam app 409710) with the BioShock Remastered VR mod. Game, mod and launcher are all 32-bit.
- The mod renders with **Direct3D 11** (its log: `D3D11 swapchain hooks installed`, `session created on the game device`).
- `xinput1_3.dll` in the game folder loads `bioshockvr.dll`; `vrpreset.ini` has `autoVr=1`, so VR starts without the launcher.
- The mod has the Khronos OpenXR loader built in. It honours `XR_RUNTIME_JSON`, `XR_API_LAYER_PATH`, `XR_ENABLE_API_LAYERS` and `DISABLE_*` variables, but ignores `XR_RUNTIME_JSON` when the game runs as administrator.
- The game folder has no `ReShade.ini`, so ReShade's 32-bit layer refuses to load. With no other 32-bit layer loading, the loader cancels VR start with -32 (see `MOD_OWN_RESHADE_CONFLICT.md`, section 2).

## What Was Built

- `ReShade-OpenXR-UI.addon32`: the same sources compiled for x86 by `build.ps1`, with its own manifest `ReShade-OpenXR-UI-Layer32.json` (same layer name, so SteamVR and the off switch treat it as the same add-on).
- No pointer-size or hook code needed changing. Only the loader entry point needed a plain-name export (x86 `__stdcall` decorates it as `_xrNegotiateLoaderApiLayerInterface@12`).
- `Install-OpenXR.cmd` and `Uninstall-OpenXR.cmd` write and remove an entry in both registry lists; an entry already at 1 (off) stays 1 (off). `package.ps1` and `verify.ps1` include the 32-bit files.

## 32-bit OpenXR Simulator

- The simulator checkout (`A:\ws\OpenXR-Simulator`) only had a 64-bit build. Its CMake build always writes the 64-bit `bin` folder, so `tools/test_bioshock_vr.ps1` compiles the same sources for x86 into its ignored `bin-x86` folder when sources are newer.
- It needs the Khronos Vulkan headers (`.deps\Vulkan-Headers`, ignored) and the linker flag `/EXPORT:xrNegotiateLoaderRuntimeInterface=_xrNegotiateLoaderRuntimeInterface@8`.
- The build succeeds without source changes and runs in BioShock: VR starts and the add-on menu renders.

## The Test (`test_bioshock_vr.bat`)

- Starts `BioshockHD.exe` directly with Steam's app ID, on the 32-bit simulator, with the build's add-on and `ReShade32.dll` as per-process layers. The installed add-on and ReShade layers are switched off for that process only.
- ReShade reads `build\bioshock-vr\reshade-base` (`RESHADE_BASE_PATH_OVERRIDE`) and runs the `DeterministicInvert` test effect, so nothing is written to the game folder.
- BioShock pauses when its window is not in front, and the simulator's preview window opens in the game process and takes focus. The script keeps the game window in front, clicks through the intros, waits for `view state: menu` in the mod log, and presses Enter until `cine edge exit` shows the game world.
- A forced kill makes BioShock offer to reset all its settings at the next start, so the script closes the window first and deletes a leftover `%APPDATA%\BioshockHD\Bioshock\Running.ini` before each start.
- `headset` runs the same setup on the real headset runtime (no capture).
- Saves `headset.png`, `ReShade.log`, `bioshockvr.log` and the simulator log to `build\bioshock-vr\last-run`.

## Results

- **Normal player install: works.** ReShade Setup on `BioshockHD.exe` with full add-on support and **OpenXR** ticked, plus `Install-OpenXR.cmd`, started with the game's own launcher on a real headset. Both registry lists have ReShade and the add-on at 0 (on), with no -32 error, because ReShade Setup puts a `ReShade.ini` in the game folder. ReShade Setup added no DLL to the game folder.
- **Real headset: works.** ReShade32 and the add-on load, the effect fallback runs on the game's D3D11 device, the invert effect shows in both eyes, and it toggles on and off from the menu. The mod renders 1066x1066 per eye and hands OpenXR two 2048x2048 images (DLSS output) at 90 fps.
- **Simulator only: effect in one eye.** In the simulator the mod takes a different path: it renders one eye per frame ("alternate-eye stereo") into two 1066x1066 swapchains, and some frames submit both eyes from the same swapchain. The add-on applies effects using the previous frame's eye layout, so when the layout changes between frames, only the right eye gets the effect. Not fixed; it matters only if a game changes its eye layout from frame to frame on a real headset. The fix would apply effects using each frame's own layout and never process one image twice as two eyes.

## Open Questions

- Does our installed 32-bit layer alone prevent the -32 error in a 32-bit game without a `ReShade.ini` while `ReShade32_XR.json` is 0 (on)?
