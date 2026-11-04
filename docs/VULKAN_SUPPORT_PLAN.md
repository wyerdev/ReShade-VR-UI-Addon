# Vulkan Support Implementation Handoff

Last confirmed: 2026-09-01

Status: [PROPOSED]

## Purpose

This document records the completed Vulkan support assessment for ReShade VR UI Add-on. Start here when implementation begins. Do not repeat the architecture search unless relevant source, dependencies, or runtime behavior have changed.

The canonical project status remains `README.md` and `DEVELOPMENT.md` in this directory. Update this plan and the applicable developer documentation when Vulkan work changes a fact, resolves an open question, or produces validation evidence.

## Decision Summary

- The expected implementation is add-on-only. Do not modify ReShade production code under `source/`, replace the official ReShade DLL, or require a custom ReShade build.
- Keep the current dual-role artifact: the same `ReShade-OpenXR-UI.addon64` remains a ReShade add-on and an OpenXR API layer.
- Reuse the existing controller actions, VIEW-space quad placement, interaction model, technique and parameter snapshots, preset operations, and shared ImGui layout.
- Add one native Vulkan backend beside D3D11 and D3D12. Do not duplicate the UI model or controls.
- Compile the existing vendored `imgui_impl_vulkan` into the add-on; do not edit the vendored backend.
- Prefer Vulkan function-pointer loading over a permanent `vulkan-1.dll` import so loading the add-on in a Direct3D-only process does not acquire a new Vulkan loader dependency.
- Use a conventional render pass initially. This avoids requiring `VK_KHR_dynamic_rendering` or Vulkan 1.3 from the target application.
- Do not assume SteamVR converts a Vulkan OpenXR binding into Direct3D. An API layer sees the application's actual `XrGraphicsBindingVulkanKHR` and `VkImage` handles.
- Touch ReShade core only after evidence proves that Vulkan queue submissions cannot be synchronized safely through the add-on. That is an escalation path, not part of the planned implementation.

## Effort Estimate

Expected effort is four to seven focused engineering days through automated simulator validation, or roughly one to two weeks including a real Vulkan OpenXR title and headset validation.

| Work | Estimate |
| --- | ---: |
| Vulkan binding, swapchain resources, commands, synchronization, and cleanup | 1-2 days |
| Private ImGui Vulkan renderer and build integration | 1 day |
| Vulkan probe, smoke, negative control, and capture integration | 1-2 days |
| Real-runtime and headset validation or hardening | 1-3 days |

The schedule risk is Vulkan queue external synchronization. If the direct add-on submission path is valid, the work should remain near one week. A verified need for a new ReShade synchronization API can push it beyond two weeks.

## Verified Current State

### Add-on architecture

- `openxr_ui_layer.cpp` currently defines only `XR_USE_GRAPHICS_API_D3D11` and `XR_USE_GRAPHICS_API_D3D12`.
- Its `graphics_api` enum, `session_state`, binding discovery, swapchain image enumeration, rendering dispatch, GPU completion, logging, and cleanup have D3D11 and D3D12 branches.
- `openxr_ui_renderer.cpp` owns one private ImGui context and one shared UI/model path. Only renderer initialization, frame setup, draw submission, and shutdown are graphics-specific.
- `openxr_ui_renderer.hpp` exposes typed D3D11 and D3D12 initialization and render overloads. A typed Vulkan overload matches the existing design better than a new virtual backend manager.
- `reshade_openxr_ui_addon.cpp`, `openxr_ui_preset.cpp`, and `openxr_ui_model.hpp` do not need graphics-specific changes.
- `build.ps1` compiles `imgui_impl_dx11.cpp` and `imgui_impl_dx12.cpp` into the add-on and renderer test. It does not compile `imgui_impl_vulkan.cpp`.
- `smoke.ps1` and `capture_simulator_ui.ps1` currently accept `Both`, `D3D11`, or `D3D12` and select either `xr_probe_d3d11.exe` or `xr_probe.exe`.

### Existing Vulkan foundations

- ReShade core already supports Vulkan OpenXR effect rendering. Its OpenXR session path recognizes a Vulkan binding, discovers the known ReShade Vulkan device and queue, and enumerates `XrSwapchainImageVulkanKHR` images.
- No ReShade core change is required merely to identify Vulkan OpenXR sessions or run effects.
- `deps\imgui\backends\imgui_impl_vulkan.cpp` and `.h` are already vendored.
- The ImGui backend supports `IMGUI_IMPL_VULKAN_NO_PROTOTYPES` and `ImGui_ImplVulkan_LoadFunctions`, so the add-on can load Vulkan entry points dynamically.
- `XrGraphicsBindingVulkan2KHR` is an alias of `XrGraphicsBindingVulkanKHR`, and `XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR` has the same value as `XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR` in the pinned OpenXR headers. One binding branch covers `XR_KHR_vulkan_enable` and `XR_KHR_vulkan_enable2` sessions.
- An external OpenXR-Simulator source checkout may provide a validation-enabled `xr_probe_vk.cpp` test client. It supports Vulkan-enable v2 by default and v1 with `XR_PROBE_VK_ENABLE1=1`.
- The external simulator bundle should contain `xr_probe.exe` and `xr_probe_d3d11.exe`; a compiled Vulkan probe is not currently part of this repository.
- On 2026-09-01, `C:\Windows\System32\vulkan-1.dll` was present, but `VULKAN_SDK` was unset and no Vulkan SDK header or import library was found through that environment variable. Recheck before implementation; do not install an SDK without telling the user what will change and receiving approval.

### Validated image-layout contract

The existing simulator Vulkan probe creates a legacy render pass over each runtime-owned `VkImage` with:

- `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` as both initial and final layout for color images.
- `VK_ATTACHMENT_LOAD_OP_CLEAR` and `VK_ATTACHMENT_STORE_OP_STORE`.
- External-to-subpass and subpass-to-external dependencies around `VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT` and `VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT`.
- No explicit image barrier before or after the render pass.

Its stated contract is that the OpenXR runtime hands an acquired color image to the application in `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`, and the application returns it in the same layout. Use this as the first implementation hypothesis. The Vulkan validation layer is the discriminating check; do not silently add guessed layout transitions.

## Product Scope

### In scope

- Vulkan OpenXR sessions using `XrGraphicsBindingVulkanKHR`.
- The add-on's own VIEW-space `XrCompositionLayerQuad` swapchain.
- The current 64-bit central current-user installation.
- Both `XR_KHR_vulkan_enable` and `XR_KHR_vulkan_enable2` application setup paths, which share the binding structure.
- Existing Oculus Touch and Khronos simple-controller behavior.
- Existing technique, parameter, preset, filtering, ordering, and persistence features.
- Automated simulator smoke, no-ReShade control, and actual simulator-window screenshots.
- User-launched real-title headset validation.

### Out of scope

- Rewriting ReShade's OpenXR Vulkan effect path.
- Modifying or redistributing a custom ReShade DLL.
- OpenGL, OpenVR, or SteamVR-dashboard UI support.
- A second UI model or Vulkan-specific controls.
- Editing vendored Dear ImGui source.
- Installing or registering Vulkan/OpenXR components without explicit approval.
- Launching a game from agent tooling.

## Planned Source Changes

### `openxr_ui_layer.cpp`

1. Define the Vulkan platform/API macros before including OpenXR platform declarations:
   - `VK_USE_PLATFORM_WIN32_KHR`
   - `VK_NO_PROTOTYPES`
   - `XR_USE_GRAPHICS_API_VULKAN`
2. Include the Vulkan SDK header before `openxr_platform.h` supplies Vulkan OpenXR structures.
3. Add `graphics_api::vulkan`.
4. Add `find_vulkan_binding` that walks the `XrSessionCreateInfo::next` chain and accepts `XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR`.
5. Retain, but do not destroy, the application-owned `VkInstance`, `VkPhysicalDevice`, `VkDevice`, queue-family index, queue index, and `VkQueue`.
6. Load `vkGetInstanceProcAddr` from the Vulkan loader only after a Vulkan binding is found. Load required instance and device entry points into explicit owned dispatch structures.
7. Select an offered quad format in this order unless runtime evidence requires adjustment:
   - `VK_FORMAT_R8G8B8A8_SRGB`
   - `VK_FORMAT_B8G8R8A8_SRGB`
   - `VK_FORMAT_R8G8B8A8_UNORM`
8. Create the existing 1024 by expanded-panel-height OpenXR swapchain with color-attachment and sampled usage.
9. Enumerate `XrSwapchainImageVulkanKHR` into owned image records.
10. Create one `VkImageView` and `VkFramebuffer` per image plus one compatible `VkRenderPass`.
11. Create a command pool for the binding's queue family, command-buffer state, and fence state. Prefer bounded fence waits over `vkQueueWaitIdle`, which would stall unrelated game work on the whole queue.
12. Record the shared ImGui draw data inside the render pass, submit it, wait for that submission to complete, and only then call `xrReleaseSwapchainImage`.
13. Log `native quad graphics backend: Vulkan.` after complete initialization.
14. Destroy only add-on-owned Vulkan objects, in dependency-safe reverse order. Never destroy the application's instance, physical device, logical device, or queue.
15. Preserve transparent forwarding when Vulkan initialization is unavailable or fails; effects and the application must continue without the UI.

### `openxr_ui_renderer.hpp` and `openxr_ui_renderer.cpp`

1. Add `implementation::graphics_api::vulkan`.
2. Add a typed initialization overload carrying the Vulkan instance, physical device, device, queue family, queue, image count, format, render pass, dimensions, and function loader needed by ImGui.
3. Compile with `IMGUI_IMPL_VULKAN_NO_PROTOTYPES` and call `ImGui_ImplVulkan_LoadFunctions` before initialization.
4. Initialize `ImGui_ImplVulkan_InitInfo` with the application's actual Vulkan objects, swapchain image count, and the add-on render pass.
5. Use an internally created descriptor pool through `DescriptorPoolSize` or an add-on-owned pool sized above `IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE`.
6. Add a render overload that calls `ImGui_ImplVulkan_NewFrame`, the existing shared `render_ui` path, and `ImGui_ImplVulkan_RenderDrawData` with the caller's command buffer.
7. Call `ImGui_ImplVulkan_Shutdown` before destroying the private ImGui context and before layer code destroys Vulkan objects used by the backend.
8. Keep all interaction behavior and layout inside the existing graphics-neutral render path.

### Build and test scripts

- `build.ps1`: add the Vulkan SDK include path and compile `imgui_impl_vulkan.cpp`. Avoid linking `vulkan-1.lib` into the add-on if dynamic loading is used. Compile or stage a Vulkan probe for the isolated simulator workflow.
- `smoke.ps1`: add `Vulkan` and preferably `All`; retain `Both` as the existing D3D11 plus D3D12 pair for compatibility. Default new comprehensive validation to `All` only after all three legs pass reliably.
- `capture_simulator_ui.ps1`: add the same selector behavior and write Vulkan captures under `simulator-captures\vulkan`.
- `verify.ps1`: continue checking required exports and artifact identity; add a dependency check proving the add-on does not acquire an unintended mandatory Vulkan loader import if dynamic loading is chosen.
- `README.md`, `INSTALL.md`, and `DEVELOPMENT.md`: update compatibility, architecture, build prerequisites, commands, and validation evidence only after support passes.
- `package.ps1`, `Install.cmd`, `Uninstall.cmd`, and the manifest should not require product-behavior changes. Verify packaging still contains the updated add-on and documentation.

## Queue Synchronization Risk

`VkQueue` host access is externally synchronized by the Vulkan specification. The OpenXR binding identifies the queue family and index, but it does not grant the API layer an independent queue or a lock shared with the application.

The first implementation should retrieve that queue with `vkGetDeviceQueue`, submit during the intercepted `xrEndFrame` path, and use an add-on fence to prove completion before releasing the quad image. This is expected to work in the deterministic simulator and in applications whose frame submission and `xrEndFrame` occur serially on one thread. It is not proof that every application serializes all access to the same queue.

Do not claim release-quality Vulkan support from a clean simulator run alone. A real Vulkan title must be tested with Vulkan validation enabled when practical, and logs must be checked for threading, lifetime, layout, and synchronization errors.

### Core-ReShade escalation rule

Keep the implementation add-on-only unless all of the following are true:

1. The Vulkan simulator path is otherwise correct and validation-clean.
2. A real application produces reproducible evidence of unsafe queue concurrency or unavailable synchronization.
3. The existing ReShade public API and native handles cannot provide a safe serialized submission path.
4. A minimal, generally useful ReShade API change can expose synchronized queue submission or equivalent ownership without coupling core ReShade to this UI.

If those conditions are met, stop and document the evidence before editing `source/`. Treat the core API work as a separately approved scope with its own tests. Do not quietly fold a custom ReShade requirement into the add-on release.

## Validation Plan

### 1. Preserve the Direct3D baseline

Before Vulkan edits, run:

```powershell
build.bat
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\smoke.ps1 -Frames 30
```

Require the existing D3D11 and D3D12 legs and both no-ReShade controls to pass before attributing later failures to Vulkan.

### 2. Establish the Vulkan toolchain

Recheck:

```powershell
$env:VULKAN_SDK
Test-Path "$env:VULKAN_SDK\Include\vulkan\vulkan.h"
Test-Path "$env:VULKAN_SDK\Lib\vulkan-1.lib"
Test-Path "$env:WINDIR\System32\vulkan-1.dll"
```

The Vulkan SDK is needed for headers, probe compilation, and validation layers. The production add-on should dynamically load runtime functions so it does not require the SDK or an import library after build time.

Compile `A:\ws\OpenXR-Simulator\probe\xr_probe_vk.cpp` against the same Khronos OpenXR loader used by the isolated bundle, then copy only the resulting probe and required test-local dependencies into `A:\ws\OpenXR-Simulator-test`. Do not register the simulator globally or modify the machine-wide active OpenXR runtime.

### 3. Focused Vulkan smoke

The future command should be:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\smoke.ps1 -GraphicsApi Vulkan -Frames 30
```

Require:

- 30 of 30 frames rendered.
- Zero probe failures.
- A live Vulkan validation callback whose deliberate probe self-test was caught.
- Zero Vulkan validation errors after that deliberate self-test is cleared.
- `native quad graphics backend: Vulkan.` in the new ReShade log.
- Add-on ImGui initialization, technique-model, quad-submission, and clean teardown evidence.
- A separate no-ReShade control that renders 30 of 30 frames without add-on activation.
- No global OpenXR registration or active-runtime change.

Run one additional focused smoke with `XR_PROBE_VK_ENABLE1=1` to cover the older `XR_KHR_vulkan_enable` setup path. The default probe covers `XR_KHR_vulkan_enable2`.

### 4. Vulkan visual capture

The future command should be:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\capture_simulator_ui.ps1 -GraphicsApi Vulkan
```

Require the same actual simulator-window states as D3D11 and D3D12:

- `main.png`
- `detail.png`
- `save-as.png`
- `creating.png`
- `created.png`
- `preset-off.png`
- `preset-on.png`
- `preset-off-again.png`

Inspect every image directly. Do not accept debug text, hit tests, file existence, hashes, or synthetic render targets as visual proof. Confirm all controls and text are visible and non-overlapping, the preset states change as expected, and the eight images are distinct where expected.

The test script must restore managed configuration and preset bytes, delete the test-created `HI.ini`, remove temporary local app data and staged layers, and leave the machine-wide runtime and registry unchanged.

### 5. Full regression

After focused Vulkan validation, run all D3D11, D3D12, and Vulkan smoke and capture legs sequentially. Preserve `Both` for the two Direct3D legs and introduce `All` for all three APIs rather than changing the historical meaning of `Both` silently.

Also run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\reshade-openxr-ui\verify.ps1
git diff --check -- tools/reshade-openxr-ui
```

Check editor diagnostics for every touched C++ and script file.

### 6. Real-title headset validation

Only the user launches the selected Vulkan OpenXR title. The agent then reads and interprets the resulting logs and artifacts.

Require:

- ReShade Vulkan and OpenXR initialization evidence.
- `native quad graphics backend: Vulkan.`
- Correct VIEW-space placement in both eyes.
- Stable pointer, trigger, thumbstick scrolling, and L3 plus R3 visibility behavior.
- Technique toggles, parameter edits, Save, Save As, Delete, and preset navigation working as they do on Direct3D.
- No Vulkan validation, device-loss, queue, layout, or teardown errors.
- The add-on remaining inert in an OpenXR process where ReShade does not accept add-on registration.

## Acceptance Criteria

Vulkan support is complete only when:

1. Production changes remain add-on-only, or a separately approved core escalation is documented with evidence.
2. D3D11, D3D12, and Vulkan smoke legs pass, including no-ReShade controls.
3. Both Vulkan-enable setup paths have at least focused smoke coverage.
4. Vulkan validation reports no errors outside its deliberate self-test.
5. All Vulkan simulator captures have been inspected directly and accepted.
6. Existing D3D11 and D3D12 behavior and captures still pass.
7. A user-launched real Vulkan OpenXR title passes headset interaction and visual checks.
8. Artifact exports, package contents, cleanup behavior, documentation, diagnostics, and `git diff --check` pass.
9. `README.md` or `DEVELOPMENT.md` records exact commands, material results, artifact size and SHA-256, installation state, unresolved risks, and the next action.

## Known Open Questions

- Which real Vulkan OpenXR title and runtime will provide the final headset test?
- Does that title serialize all use of the binding queue with `xrEndFrame`, or is coordination through ReShade required?
- Which Vulkan SDK version should be used to compile the local probe and add-on on this machine?
- Will the target runtime offer the preferred sRGB format, or require the BGRA/unorm fallback?
- Does the central full-add-on ReShade installation expose the Vulkan effect runtime and registration timing exactly as the simulator does?

Resolve these through the validation sequence above. Do not reopen already settled questions about UI architecture, SteamVR translation, or whether a custom ReShade build is required unless new evidence contradicts this document.

## Start Here

1. Finish the existing real D3D12 headset validation so the current dual-backend release has a stable checkpoint.
2. Recheck the Vulkan SDK and loader commands above.
3. Compile and stage `xr_probe_vk.cpp` in the isolated simulator bundle.
4. Add the smallest Vulkan binding and initialization branch to `openxr_ui_layer.cpp`.
5. Add the typed ImGui Vulkan renderer overload.
6. Run the focused 30-frame Vulkan smoke before extending capture or documentation.

The first substantive implementation edit must be followed immediately by that narrowest available build or Vulkan smoke check. If the direct queue submission hypothesis fails, document the exact validation evidence and apply the core-ReShade escalation rule instead of broadening changes speculatively.