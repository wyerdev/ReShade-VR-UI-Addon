# DOOM 2016 VR mod crashes with ReShade's Vulkan layer

## Summary

DOOM (Vulkan) with the KHARVOX DOOM 2016 VR mod (v1.11, SFS renderer, SteamVR) works without ReShade and crashes about 5 to 12 seconds after start with ReShade's Vulkan layer installed. The crash also happens with this add-on removed, so the add-on is not the cause. The ReShade OpenXR layer is disabled by the mod's launcher in these runs.

## Setup

- ReShade 6.8.0.2155 (official install, Vulkan layer registered).
- Mod launcher with its own Vulkan layer (`VK_LAYER_KHARVOX_OPENXR`) and SteamVR as the OpenXR runtime.
- Vulkan loader 1.4.341.0, SteamVR runtime `vrclient_x64.dll` 1.1.1.0, NVIDIA driver.
- Other Vulkan layers in the process: RivaTuner, Steam overlay, EOS overlay.

## Cause

1. ReShade's Vulkan layer switches on the optional debug-label feature (`VK_EXT_debug_utils`) for every Vulkan instance it sees. The mod passes that setup on to SteamVR, so SteamVR logs `Debug utils: enabled` and starts naming and labelling its GPU work.
2. With the SFS renderer, the mod gives SteamVR its GPU-work functions straight from the driver, skipping the Vulkan loader. The command buffers SteamVR creates that way are never prepared by the loader (their first 8 bytes still hold the placeholder `0x01CDC0DE`, `ICD_LOADER_MAGIC`).
3. The driver does not offer the debug-label functions, so the mod's lookup falls back to the Vulkan loader's versions. The loader's label function reads the missing setup of the driver-only command buffer and crashes (`0xC0000005` in `vulkan-1.dll`).
4. Without ReShade the debug-label feature stays off, SteamVR writes no labels, and the mismatch is never reached.

So the fault is the mod mixing two kinds of function routes; ReShade only switches on the feature that exposes it. Neither this add-on nor ReShade can fix it without a workaround.

## Evidence from the crash dump

- The crash is in the Vulkan loader's debug-label pass-through, whose error text is `VK_EXT_debug_utils: Invalid device handle`; the label structure type is `0x3B9CBE02` (`VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT`).
- SteamVR's function table for this work mixes NVIDIA driver functions (`nvoglv64.dll`) and loader functions (`vulkan-1.dll`).
- The bad object is one of 12 command buffers that SteamVR keeps in the same object, and it points into the NVIDIA driver.
- A clean 54-minute run without ReShade selected the same queue and never reached this path.

## Not yet known

- Whether the mod's AER renderer, which does not use the driver-direct route, runs with ReShade.

## Reproduce and debug

`debug_doom_crash.bat` waits for `DOOMx64vk.exe`, attaches the WinDbg console debugger, and writes the log and a dump to `build\doom-dumps`. The mod writes its own log to `%TEMP%\KHARVOX.log`.
