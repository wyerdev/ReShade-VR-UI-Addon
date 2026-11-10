# Vulkan Support For OpenXR Games

Last confirmed: 2026-11-10

Status: [BUILT, TESTED IN THE SIMULATOR; NOT YET CONFIRMED IN A GAME]

The headset menu and FPS counter are drawn in OpenXR games that use Vulkan. ReShade already ran its effects there; only the menu was missing. Vulkan SteamVR (OpenVR) games are unchanged: they use ReShade's own SteamVR dashboard menu.

## What Was Built

- Add-on only. No ReShade change, no custom ReShade build.
- `openxr_ui_vulkan.cpp` (`vulkan_quad_device`) owns everything the menu creates on the game's Vulkan device: one render pass, an image view and framebuffer per menu image, four command buffers and their fences.
- `openxr_ui_layer.cpp` reads the game's `XrGraphicsBindingVulkanKHR`. One branch covers `XR_KHR_vulkan_enable` and `XR_KHR_vulkan_enable2`, because both use the same structure.
- `openxr_ui_renderer.cpp` draws with Dear ImGui's vendored Vulkan backend (`initialize_vulkan`, `render_vulkan`, `render_statistics_vulkan`). The menu layout and controls are the shared ones; nothing is duplicated.
- Vulkan is reached only through function pointers taken from the `vulkan-1.dll` the game already loaded. The add-on has no `vulkan-1.dll` import; `verify.ps1` fails the build if one appears.
- The build needs the Khronos Vulkan headers in the ignored `.deps\Vulkan-Headers` folder (`build-addon.bat` downloads them when missing). No Vulkan SDK is needed.

## Decisions And Why

- **The game's queue is used only inside `xrEndFrame`.** A Vulkan queue may be used by one thread at a time. The OpenXR Vulkan rules make the game keep its other threads off the session's queue during `xrEndFrame` (the runtime itself uses the queue there), so drawing the menu at that point needs no lock shared with the game.
- **No waiting on the GPU per frame.** Each menu drawing takes the next of four command buffers. Its fence is only read; when that buffer is still in use the menu skips the frame and the runtime shows the last menu image again. Same idea as the Direct3D 12 path.
- **Image layout.** OpenXR hands an acquired color image over in `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` and wants it back that way, so the render pass uses that layout at both ends and adds no layout changes of its own.
- **Conventional render pass**, not dynamic rendering, so the game needs neither Vulkan 1.3 nor `VK_KHR_dynamic_rendering`.
- **Session end.** A game may destroy its Vulkan device right after `xrDestroySession` (the simulator's test program does). The layer therefore waits, at most one second and only then, for its last menu drawings and destroys its objects before returning. A session the game never ends (process exit) leaves its Vulkan objects alone, because the device may already be gone.
- **Window functions.** Dear ImGui's backend asks for the window swapchain functions of its multi-window mode when it loads Vulkan. A game that only draws to the headset may not have them switched on, so the renderer hands the backend a stand-in for those; the menu never calls them.
- **Font texture upload.** As in the Direct3D 12 backend, Dear ImGui waits for the queue once when the font texture changes (normally only on the first frame).
- **Formats.** `VK_FORMAT_R8G8B8A8_SRGB` is preferred, `VK_FORMAT_R8G8B8A8_UNORM` is the fallback.
- **Effect fallback** (the add-on's own effect runtime for UEVR) stays Direct3D only.

## Test Results (2026-11-10, OpenXR Simulator)

- `smoke.ps1 -GraphicsApi Vulkan -Frames 30`: 30 of 30 frames, 0 failures, ReShade's effect runtime and the menu on the same Vulkan device, no-ReShade control passed. Passed for `XR_KHR_vulkan_enable2` and, with `XR_PROBE_VK_ENABLE1=1`, for `XR_KHR_vulkan_enable`.
- `capture_simulator_ui.ps1 -GraphicsApi Vulkan`: the menu and FPS counter look the same as with Direct3D; sampled menu colors are identical to the Direct3D 12 capture.
- The 64-bit and 32-bit add-ons build; neither imports `vulkan-1.dll`.

How the test runs: the simulator bundle has no Vulkan test program, so `build_vulkan_probe.ps1` builds the simulator's own `probe\xr_probe_vk.cpp` into `build\reshade-openxr-ui\vulkan-probe`. ReShade reaches Vulkan programs through a Vulkan layer, so the test loads the bundle's ReShade as that layer and switches off the ReShade installed on the PC, for the test process only.

## Still Open

- **No run in a real game yet.** First candidate: Alan Wake with its VR mod (32-bit, Direct3D 9 converted to Vulkan, `XR_KHR_vulkan_enable`), where the missing menu was reported. Check: menu visible in both eyes, pointer and buttons work, effects still run, no crash on exit, frame rate with and without the add-on.
- **Vulkan validation layer.** It is not installed on the test PC (it comes with the Vulkan SDK), so the simulator runs could not check image layouts or threading. Installing the SDK needs the user's approval.
- **32-bit Vulkan** is built but untested; the simulator tests are 64-bit.
- A runtime that offers neither format above gets no menu (`no RGBA8 swapchain format is available` in the log). Add `VK_FORMAT_B8G8R8A8_SRGB` if a runtime needs it.

## If A Game Shows Queue Problems

Keep the add-on-only design unless a real game gives repeatable evidence that it uses the session's queue from another thread during `xrEndFrame`. That game would be breaking the OpenXR Vulkan rules, and the runtime's own queue use would be affected too. Document the evidence before considering any ReShade change.
