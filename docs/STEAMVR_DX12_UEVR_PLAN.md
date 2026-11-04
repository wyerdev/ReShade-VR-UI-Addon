# Future Plan: SteamVR + DirectX 12 + UEVR Crash (Fix In ReShade)

## Problem

With SteamVR's OpenXR runtime, DirectX 12 UEVR games crash or freeze on the first VR frame while ReShade's OpenXR layer is on, even without the add-on.

- Tested failing: Gothic 1 Remake (crash, Unreal "PresentInternal failed ... 80004004" = E_ABORT), Star Wars Jedi: Survivor (freeze). Probably also Star Wars Galactic Racer (player report, unconfirmed).
- Works: the same games with the ReShade layer off; DirectX 11 UEVR games (Star Wars Jedi: Fallen Order); Pimax's own OpenXR runtime.

## Cause (Confirmed By The Gothic Test)

1. For DirectX 12 apps, SteamVR's OpenXR runtime calls `D3D11On12CreateDevice` on the first frame (`xrclient_<exe>.txt` logs "enable advanced image processing of scene textures" in the same millisecond).
2. ReShade hooks it (`source\d3d11\d3d11on12.cpp`). UEVR's DirectX 12 device is not a ReShade proxy, so ReShade logs "Skipping D3D11on12 device because it was created without a proxy Direct3D 12 device." and calls the trampoline (line 40) **without** setting `g_in_dxgi_runtime`.
3. Inside, D3D11On12 creates its own DXGI factory (`CreateDXGIFactory2`, Flags `0x80000000`). Because `g_in_dxgi_runtime` is false, ReShade wraps it in its `DXGIFactory` proxy (`source\dxgi\dxgi.cpp`, around lines 658 and 752).
4. That wrapped factory is the last line of ReShade's log in every failed run. ReShade's normal path (lines 58–60) sets `g_in_dxgi_runtime = true` around the same call exactly to avoid this.

DirectX 11 games never call `D3D11On12CreateDevice`, and Pimax's runtime doesn't use it, which fits.

Likely an oversight in ReShade: the skip branch dates from 2022 (commit `8426af40`), before ReShade began wrapping DXGI factories in a proxy (June 2025, `d43daf0a`/`4a3bd6f9`). It was never updated for that change.

## Status

The fix is applied in the ignored `.deps\reshade` checkout and built as a normal Release build (`Release|64-bit`, version 6.8.0, full add-on support) at `.deps\reshade\bin\x64\Release\ReShade64.dll`. Gothic 1 Remake with UEVR on SteamVR (OpenXR) no longer crashes with it (user test), which confirms the cause. Still to test: Jedi Survivor, Fallen Order and Pimax's runtime, then report it to ReShade.

## Fix To Try

In `d3d11on12.cpp`, the skip branch (around lines 38–40): set `g_in_dxgi_runtime = true` around the trampoline call, as the normal path does:

```cpp
g_in_dxgi_runtime = true;
const HRESULT hr = trampoline(pDevice, Flags, pFeatureLevels, FeatureLevels, ppCommandQueues, NumQueues, NodeMask, ppDevice, ppImmediateContext, pChosenFeatureLevel);
g_in_dxgi_runtime = false;
return hr;
```

### Expected Side Effects

None expected:

- On this path ReShade has already decided not to wrap SteamVR's helper device, so it also leaves alone what that device creates inside.
- ReShade already does the same for the Steam overlay's helper device and on its own normal path.
- Only programs that call `D3D11On12CreateDevice` with a device ReShade did not wrap reach this code. DirectX 11 games and other runtimes never do.

## Test Steps

1. Make the change in the `.deps\reshade` checkout only, and build `ReShade64.dll` (`Release|64-bit`).
2. Swap in the test DLL: double-click `deploy_reshade.bat` in the repository root. It asks for administrator approval, backs up `C:\ProgramData\ReShade\ReShade64.dll` (the machine-wide ReShade OpenXR layer) once to `ReShade64.dll.BAK`, and copies the test DLL in. Running it again keeps the first backup. Make sure the game folder has no ReShade `dxgi.dll` of its own, so only the layer loads ReShade.
3. The user runs Gothic 1 Remake with UEVR on SteamVR (OpenXR), ReShade layer on, no add-on: it must not crash, and ReShade's log must no longer show the wrapped factory after "Skipping D3D11on12 device".
4. Repeat with the add-on installed: effects and menu in the headset.
5. Repeat with Jedi Survivor (freeze case) and Fallen Order (DirectX 11, must still work), and once on Pimax's runtime.
6. Restore the original: double-click `restore_reshade.bat`.
7. If it holds, report it to ReShade upstream with the logs. The add-on docs stay unchanged until an official ReShade release contains the fix.
