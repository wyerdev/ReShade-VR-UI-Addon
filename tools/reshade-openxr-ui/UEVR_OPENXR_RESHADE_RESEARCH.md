# UEVR OpenXR ReShade Research

Date recorded: 2026-09-06

Status: Research only. No UEVR compatibility implementation has been approved
or added to this repository.

> Superseded 2026-09-24: its "the add-on cannot fix this" conclusion is replaced
> by `UEVR_RESHADE_RESEARCH.md` and `UEVR_RESHADE_PLAN.md`.

## Short Answer

ReShade has one way to process the monitor image and another way to process VR
headset images.

When ReShade is installed normally, it starts with the game and changes the
monitor image. UEVR later creates separate left-eye and right-eye images for
the headset, so the monitor effects are not automatically included in VR.

When ReShade is installed for OpenXR, Windows loads ReShade when an application
starts OpenXR. This works for ordinary OpenXR games because they normally start
OpenXR before creating the DirectX parts used for VR.

UEVR does the opposite. The game creates DirectX first, then UEVR is added, and
only then does UEVR start OpenXR. Windows does load the ReShade OpenXR layer,
but ReShade cannot take control of the DirectX parts that were created before
it arrived. ReShade therefore leaves the UEVR headset images unchanged.

The official ReShade injector may help only if it loads official ReShade very
early, before the game creates DirectX. Loading ReShade when UEVR starts is
already too late. This early-loading idea has not yet been proven, especially
for DirectX 12.

Our add-on cannot fix this by itself. It can display and control VR effects
after ReShade has created them, but it cannot make official ReShade accept an
OpenXR session that ReShade has already refused.

## Purpose

This note records why a normal ReShade OpenXR installation can affect a native
OpenXR game's headset image but does not currently affect the headset image
produced by UEVR.

The required product boundaries are:

- Continue using the official ReShade runtime. Do not build or distribute a
  custom ReShade version.
- Keep the existing globally registered OpenXR UI layer.
- Do not add a game-local `dxgi.dll`, add-on, or configuration workaround.
- Do not modify game files or launch a game during automated validation.
- Preserve both Direct3D 11 and Direct3D 12 support.

## Terms Used In This Note

Only a few technical names are necessary:

- **DirectX device:** The main graphics object the game uses to communicate
   with the graphics card.
- **DirectX command queue:** A DirectX 12 object used to send graphics work to
   the graphics card.
- **ReShade-controlled device:** A DirectX device that ReShade saw being
   created and placed under its control. ReShade source code calls this a
   "proxy device."
- **OpenXR layer:** A DLL that Windows automatically places between an OpenXR
   application and the active headset runtime.
- **OpenXR session:** The connection an application creates with the headset
   runtime.
- **OpenXR swapchain:** The set of left-eye or right-eye images that will be
   displayed in the headset.
- **VR effect runtime:** The part of ReShade that loads effects and applies
   them to the headset images. This is separate from the ReShade instance that
   processes the monitor image.

## Normal ReShade Install Versus OpenXR-Layer Install

ReShade has two different ways to enter a game. They may use the same official
ReShade runtime, but Windows loads that runtime at different times and for
different reasons.

### Normal game-local installation

A normal DirectX installation places the ReShade DLL beside the game and gives
it the name of a graphics-system DLL, commonly `dxgi.dll`.

The simplified sequence is:

1. The game starts.
2. The game asks Windows to load `dxgi.dll` as part of starting DirectX.
3. Windows finds the game-local ReShade proxy DLL first.
4. ReShade loads the real Windows graphics DLL and places its own wrappers
   around the DirectX device, command queue, and desktop swapchain.
5. ReShade applies effects when the game presents an image to the monitor.

This installation is early because the game needs DirectX near the beginning
of startup. It is also game-specific because the proxy DLL and usually the
ReShade configuration are stored beside that game's executable.

Desktop ReShade normally processes the monitor backbuffer. It does not make
every other texture created by the game pass through the same effect runtime.

### OpenXR-layer installation

An OpenXR installation does not need a `dxgi.dll` beside the game. ReShade
Setup installs the official runtime in a shared machine directory, installs an
OpenXR API-layer manifest, and registers that manifest with Windows.

The simplified sequence for a native OpenXR game is:

1. The game calls `xrCreateInstance` to start OpenXR.
2. The Windows OpenXR loader reads the registered API-layer manifests.
3. The loader loads the shared official ReShade DLL as an OpenXR layer.
4. ReShade installs its DirectX hooks.
5. The game creates the DirectX objects it will use with OpenXR.
6. ReShade wraps those objects.
7. The game creates its OpenXR session and swapchains with the wrapped objects.
8. ReShade applies effects to the left-eye and right-eye images before
   `xrEndFrame` submits them to the headset.

This installation is global rather than game-local. ReShade is loaded because
the application starts OpenXR, not because the application loads a local
graphics proxy DLL.

### Why the difference matters for UEVR

UEVR reverses the important part of the OpenXR sequence:

1. The game starts DirectX and creates its graphics objects while it is still
   an ordinary monitor game.
2. UEVR is injected later.
3. Only then does UEVR call `xrCreateInstance`.
4. Windows loads the registered ReShade OpenXR layer correctly.
5. UEVR gives OpenXR the DirectX objects that already existed before the layer
   loaded.

The OpenXR installation is therefore activated, but too late to wrap those
objects in the way the official ReShade OpenXR implementation expects.

If both forms of ReShade are installed, desktop effects can still work through
the game-local `dxgi.dll` while VR effects fail through the separate OpenXR
path. Seeing effects on the monitor only proves that the desktop swapchain has
a working ReShade runtime. It does not prove that ReShade accepted UEVR's
OpenXR session or created a VR effect runtime for the headset images.

## The Actual Problem

ReShade is not failing to notice that UEVR eventually activates OpenXR. The
Windows OpenXR loader discovers registered implicit API layers when UEVR calls
`xrCreateInstance`, even though the game process has already been running.

The failure happens because UEVR starts OpenXR after the game has already
created DirectX. ReShade's OpenXR layer needs DirectX objects that ReShade has
controlled from the time they were created. UEVR instead supplies the objects
that the game was already using. ReShade sees the OpenXR session but refuses to
create the part that would apply effects to the headset images.

This is separate from normal desktop ReShade behavior. A game-local ReShade
installation can process the monitor backbuffer while UEVR copies different
render textures into OpenXR swapchains for the headset. Effects visible on the
monitor therefore do not automatically appear in VR.

## Loading Sequences

### Native OpenXR Application

A compatible native OpenXR application normally performs these operations in
this order:

1. The application calls `xrCreateInstance`.
2. The OpenXR loader reads the registered implicit API-layer manifests.
3. The loader loads the official ReShade OpenXR layer.
4. ReShade installs its DirectX hooks.
5. The application creates its DirectX device and command queue.
6. ReShade returns wrapped, or proxy, DirectX objects to the application.
7. The application passes those wrapped objects to `xrCreateSession`.
8. ReShade recognizes its objects and creates an OpenXR VR effect runtime.
9. This project's add-on registers with that runtime and controls its effects.

### UEVR OpenXR Application

UEVR necessarily starts from a different order:

1. The non-VR game starts and creates its DirectX device, command queue, and
   desktop swapchain.
2. UEVR is injected into the already-running game.
3. UEVR obtains the existing DirectX objects from the game's swapchain.
4. UEVR loads `openxr_loader.dll` and calls `xrCreateInstance`.
5. The OpenXR loader discovers and loads the registered ReShade and ReShade VR
   UI layers.
6. UEVR passes its existing DirectX device and, for Direct3D 12, command queue
   to `xrCreateSession`.
7. ReShade checks whether those objects are ReShade proxy objects.
8. The check fails, so ReShade does not create an OpenXR VR effect runtime.
9. UEVR continues submitting headset images without ReShade effects.

The important difference is that step 1 occurs before the ReShade OpenXR layer
exists in the UEVR sequence.

## Technical Evidence

This section keeps the exact function names, source files, and log messages
needed by a developer to verify the explanation. It is not necessary for
understanding the conclusion above.

### The OpenXR loader discovers layers at `xrCreateInstance`

The Khronos loader calls `ApiLayerInterface::LoadApiLayers` from
`LoaderXrCreateInstance`. Layer discovery is therefore tied to OpenXR instance
creation, not to Windows process creation.

Relevant local dependency source:

- `.deps/reshade/deps/openxr/src/loader/loader_core.cpp`
- `.deps/reshade/deps/openxr/src/loader/api_layer_interface.cpp`

The official ReShade manifest registers `ReShade64.dll` as the implicit layer
`XR_APILAYER_reshade`:

- `.deps/reshade/setup/ReShade64_XR.json`

Consequently, UEVR starting OpenXR late still gives the loader an opportunity
to load ReShade. Merely re-registering the layer or detecting
`openxr_loader.dll` later does not address the DirectX-object problem.

### UEVR passes its existing DirectX objects

UEVR gets the active device from its DirectX hook and places that pointer in
the OpenXR graphics binding:

- Direct3D 11 assigns the existing device to
  `XrGraphicsBindingD3D11KHR::device`.
- Direct3D 12 assigns the existing device and command queue to
  `XrGraphicsBindingD3D12KHR::device` and `queue`.

Relevant upstream UEVR source:

- `src/hooks/D3D11Hook.cpp`
- `src/hooks/D3D12Hook.cpp`
- `src/mods/vr/D3D11Component.cpp`
- `src/mods/vr/D3D12Component.cpp`

UEVR also deliberately bypasses existing ReShade and overlay hooks while it
creates dummy DirectX objects used to find hook targets. This protects those
dummy objects from overlays, but UEVR's later OpenXR graphics binding still
comes from the already-running game's graphics path.

### ReShade requires DirectX objects it controls

ReShade places a private marker on every DirectX device it controls. When an
OpenXR session starts, ReShade looks for that marker before creating its VR
effects system.

For DirectX 11, ReShade needs to recognize the device. For DirectX 12, ReShade
needs to recognize both the device and the command queue. The source code calls
these recognized objects proxies.

If the device check fails, ReShade logs one of these messages:

```text
Skipping OpenXR session because it was created without a proxy Direct3D 11 device.
Skipping OpenXR session because it was created without a proxy Direct3D 12 device.
```

Relevant local dependency source:

- `.deps/reshade/source/openxr/openxr_hooks_swapchain.cpp`
- `.deps/reshade/source/d3d11/d3d11_device.cpp`
- `.deps/reshade/source/d3d12/d3d12_device.cpp`
- `.deps/reshade/source/d3d12/d3d12_command_queue.cpp`

UEVR issue 348 reports the same Direct3D 12 warning after installing ReShade
for both DirectX and OpenXR:

- <https://github.com/praydog/UEVR/issues/348>

### Why ReShade refuses objects it does not control

The marker does more than identify ReShade. It leads to the information
ReShade needs to:

- create and track effect resources;
- issue rendering and copy commands;
- transition Direct3D 12 resource states;
- synchronize work on the correct command queue;
- collect the OpenXR eye images into a ReShade VR effect runtime; and
- copy processed images back before `xrEndFrame` reaches the runtime.

Without that information, ReShade could send work in the wrong order, damage a
frame, or crash the game. It therefore leaves the headset image alone instead
of guessing.

### What the official ReShade OpenXR path normally does

After accepting a session, ReShade tracks OpenXR swapchains and their acquired
and released images. At `xrEndFrame`, it finds the projection views, processes
the eye textures through a side-by-side VR effect runtime, and copies the
results back before forwarding the frame.

Relevant local dependency source:

- `.deps/reshade/source/openxr/openxr_hooks_swapchain.cpp`
- `.deps/reshade/source/openxr/openxr_impl_swapchain.cpp`

The UEVR failure occurs before this processing is initialized.

## What This Means For Our Add-on

This project's OpenXR layer is also discovered when UEVR calls
`xrCreateInstance`. Its private renderer can create a headset quad from the raw
DirectX binding, so loading the UI layer is not itself the missing behavior.

The UI controls need ReShade's VR effects system to exist. Our add-on can
control that system after ReShade creates it. It cannot force official ReShade
to reconsider a session that ReShade rejected, and it cannot take an old
DirectX device and place it under ReShade's control using the public add-on
features.

Relevant repository source:

- `tools/reshade-openxr-ui/openxr_ui_layer.cpp`
- `tools/reshade-openxr-ui/reshade_openxr_ui_addon.cpp`

## Can The Official Injector Help?

### Loading ReShade when UEVR activates is too late

Loading official ReShade when `openxr_loader.dll` appears, immediately before
or after UEVR calls `xrCreateInstance`, does not repair the existing DirectX
objects. The game created them before UEVR was injected.

The official injector can load `ReShade64.dll`, but it cannot go back in time
and change how the existing DirectX device and command queue were created.

### Loading official ReShade before the game creates DirectX

A different experiment is possible without creating a custom ReShade build:

1. Start the official injector before starting the game and tell it which game
   to wait for.
2. Let it inject the same official `ReShade64.dll` used by the registered
   OpenXR layer as soon as the game process appears.
3. Official ReShade starts watching DirectX before the game creates its
   graphics objects.
4. Inject UEVR later and let it activate OpenXR normally.

This is not the same as loading ReShade when UEVR activates. The goal is for
ReShade to control the game's DirectX objects from their creation, so the
OpenXR layer can recognize them later.

This path is not yet proven:

- The official injector deliberately waits briefly after finding the process,
  so it may lose the race against early DirectX initialization.
- DirectX 12 also needs the ReShade-controlled command queue. UEVR currently
   obtains the original queue, so loading ReShade early may still be
   insufficient.
- Generic injected ReShade normally selects the game directory as its base
  path. Avoiding game-local configuration and log writes still requires a
  separate, approved base-path solution.
- Direct3D 11 and Direct3D 12 must be validated independently.

The official injector is therefore a useful experiment, not a confirmed UEVR
solution.

## Possible Solutions Without A Custom ReShade Build

### 1. Change what UEVR gives to OpenXR

The cleanest long-term route is for UEVR to find the DirectX device and command
queue controlled by ReShade and give those to OpenXR instead of the original
objects.

Challenges:

- ReShade does not currently provide a supported public way for UEVR to ask for
   these objects.
- DirectX 12 needs both the correct device and the correct command queue.
- An ordinary UEVR plug-in starts too late to change the objects used to create
   the OpenXR session.
- The change therefore belongs in UEVR itself, or in a new public connection
   designed jointly by UEVR and ReShade.

### 2. Load unmodified ReShade early

Start the official injector before the game so official ReShade can control
DirectX before UEVR exists. This keeps ReShade unchanged, but the timing is not
guaranteed, DirectX 12 may still provide the wrong command queue, and ReShade
may try to store files in the game directory.

### 3. Make our layer process the headset images itself

In principle, our layer could collect UEVR's left-eye and right-eye images, ask
ReShade to run effects on them, and copy the changed images back before they
reach the headset.

This would replace much of what ReShade's official OpenXR layer normally does.
It would require a large amount of DirectX 11 and DirectX 12 graphics work and
would carry significant compatibility risk. It is not the preferred path.

### 4. Ask official ReShade to support existing DirectX objects

The official ReShade project could add support for an OpenXR application that
starts with an already-existing DirectX device and command queue. This would
fix the problem directly. A private custom ReShade build is not allowed, so
this option is useful only if the change becomes part of an official release.

## Approaches That Do Not Solve The Problem

- Re-registering the ReShade OpenXR manifest. The layer is already discovered
  when UEVR calls `xrCreateInstance`.
- Waiting for `openxr_loader.dll` and then loading ReShade. The DirectX objects
  already exist by then.
- Loading only `ReShade-OpenXR-UI.addon64`. An add-on controls ReShade after a
  VR effect runtime exists; it does not create the official runtime.
- Using desktop ReShade output as proof of VR support. The monitor backbuffer
  and UEVR's OpenXR eye textures are different render targets.
- Registering a normal UEVR plug-in after OpenXR session creation. That is too
  late to replace the graphics binding passed to `xrCreateSession`.

## Recommended Next Direction

Because a custom ReShade build is not allowed, the preferred investigation is
a change to UEVR:

1. Determine whether UEVR can obtain the DirectX device and command queue that
   ReShade controls through a supported method.
2. If no supported method exists, request one from the ReShade project instead
   of depending on private implementation details that may change.
3. Make UEVR give those objects to OpenXR when it creates the session.
4. Leave the official ReShade OpenXR layer responsible for swapchain tracking,
   stereo effect processing, and runtime creation.
5. Leave this project's add-on responsible only for the headset UI and ReShade
   controls.

The official early-injector workflow can be tested in parallel as a narrow
compatibility experiment, especially for Direct3D 11, but it should not be
presented as complete until both graphics APIs and the no-game-write boundary
pass validation.

## Validation Requirements For Future Work

Do not claim UEVR support from the standalone OpenXR Simulator. UEVR has a
different graphics-object lifecycle and requires a real UEVR test.

For any future prototype:

1. Build this repository and run its private renderer tests.
2. Run `verify.ps1`.
3. Run the existing OpenXR Simulator D3D11 and D3D12 smoke tests as regression
   checks only.
4. Confirm that the official ReShade OpenXR layer appears in logs after UEVR
   calls `xrCreateInstance`.
5. Confirm that ReShade no longer logs the missing proxy-device warning.
6. For Direct3D 12, confirm that ReShade accepts the command queue and creates
   the VR runtime.
7. Confirm that a distinct windowless VR effect runtime is initialized and
   that this add-on enumerates its techniques.
8. Confirm no game-local file is created or modified by the test workflow.
9. Let the user launch the game and inject UEVR.
10. Inspect ReShade and UEVR logs directly after the run.
11. Require a headset observation showing that an obvious test effect changes
    both eyes while the add-on UI remains usable.

## Current Conclusion

UEVR does activate the registered ReShade OpenXR layer. The layer then rejects
the session because UEVR supplies DirectX objects that were created before the
layer could wrap them. Loading ReShade only when UEVR activates cannot repair
those existing objects.

With official ReShade kept unchanged, the two credible directions are changing
UEVR so it gives OpenXR the ReShade-controlled DirectX objects, or loading the
official ReShade DLL before the game creates DirectX. Changing UEVR is the
cleaner complete design. Early loading is an unproven workaround with timing,
DirectX 12, and file-location risks.