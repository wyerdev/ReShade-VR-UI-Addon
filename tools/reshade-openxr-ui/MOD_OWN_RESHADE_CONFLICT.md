# Games That Bring Their Own ReShade (D2R-PCVR)

Found with the D2R-PCVR mod for Diablo II: Resurrected (mod 0.3.24, ReShade 6.8.0
bundled in `mods\D2R-PCVR\reshade`, started through `D2RLoader.exe`).

## What Happens

Two separate problems showed up together.

### 1. The game crashes a few seconds after entering the game world

- The mod loads its own copy of ReShade into the game and uses it for its own fog,
  sky and depth shaders.
- Our add-on is installed for all OpenXR games, so the OpenXR loader also loads it
  into the game. When it starts, it finds the mod's ReShade and attaches to it.
- Title screen and character select work, and our panel shows.
- When the game world starts, our add-on sees that no official ReShade VR effect
  runtime exists and starts its own effect fallback: a second ReShade effect
  runtime on a 7952x3924 headset image, on the game's Direct3D 12 device.
- The mod's log stops about two seconds later: the game has crashed.

Evidence (`ReShade.log` and `d2r-vr-frame.log` in the mod folder, 10:13 run):

- `Registered add-on "ReShade VR UI Add-on"` inside the mod's ReShade.
- 10:14:41.176 mod: `stereo gameplay active`.
- 10:14:41.634 add-on: `effect fallback started a ReShade effect runtime for 7952x3924 headset images`.
- 10:14:44.514 last line in the mod's log.

### 2. After uninstalling our add-on, the game no longer starts in VR

This is not caused by our add-on, but our add-on was hiding it.

- The central ReShade install (`%ProgramData%\ReShade`) registers itself as an
  OpenXR layer for all games. Its DLL only loads when a `ReShade.ini` exists
  next to the game's `.exe` (ReShade Setup creates one for each picked game).
  In other games, such as `D2RLoader.exe`, it refuses to load.
- The official OpenXR loader has a flaw: if every extra layer (implicit API layer)
  fails to load and none loads, it cancels VR start with `XR_ERROR_FILE_ACCESS_ERROR`
  (-32). See `ApiLayerInterface::LoadApiLayers` in the Khronos OpenXR-SDK
  (`src/loader/api_layer_interface.cpp`): a failed load sets that error while
  `any_loaded` is false, and only a later successful load clears it.
- With our layer installed, it loads successfully and clears the error, so VR
  started. Without it, only the failing ReShade layer remains and VR never starts.
- The mod's log shows `create instance failed (-32)` in every run while our
  add-on was uninstalled (09:55 to 10:12).
- 32-bit OpenXR games hit this whenever ReShade's 32-bit layer refuses to load,
  because our add-on has no 32-bit layer to clear the error. ReShade Setup
  registers both `ReShade64_XR.json` and `ReShade32_XR.json` (the latter under
  `HKLM\SOFTWARE\WOW6432Node\Khronos\OpenXR\1\ApiLayers\Implicit`), set to 0,
  whenever ReShade is installed with **OpenXR** ticked, for any game. Seen with the
  BioShock Remastered VR mod: -32 on both its Pimax and SteamVR paths; setting
  that value to 1 (off) made VR start. The player fix is in `OPENXR-LAYERS.md`.

### Confirmed

Starting the game with both layers switched off for that one launch made VR work
without a crash:

```powershell
$env:DISABLE_XR_APILAYER_reshade_vr_ui = '1'   # our add-on
$env:DISABLE_XR_APILAYER_reshade_1     = '1'   # central ReShade OpenXR layer
$env:XR_RUNTIME_JSON = 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json'
Set-Location '<D2R game folder>'
.\D2RLoader.exe -mod D2R-PCVR
```

This is a workaround for testing, not the fix.

## What Was Built (Option A, 2026-10-23)

- **Detection, automatic, no game list.** The first time it is needed (at the
  latest when VR starts), the add-on looks at the ReShade it attached to. If
  that ReShade's DLL is the library of an enabled OpenXR layer (the registry
  layer lists, or a manifest found through `XR_API_LAYER_PATH`, which the test
  scripts use), it is the normal setup. Otherwise the game brought its own
  ReShade, and the add-on writes one log line saying so.
- **In such games the add-on:**
  - never starts the effect fallback, and does not add copy flags to the
    game's headset images;
  - never switches off effects on the game's window runtime;
  - points the panel at that window runtime, so the panel lists the game's
    techniques and presets. ReShade does not tell late add-ons about runtimes
    that already exist, so the panel also picks the runtime up from its first
    frame.
- Our layer still loads normally, so the loader problem (2) cannot happen.
- Tested: `build.bat` passes; the smoke test passes for D3D11 and D3D12 (normal
  setup still recognised).
- Agent-only simulator run (`tools\test_d2r_pcvr.ps1`): the game, VR and the
  add-on start, but the run stopped at the D2R start screen. That screen only
  looks frozen; it waits for someone to click the window, press a key and click
  Play. The agent cannot do that, so this run did not reach the game world and
  is not a crash test. The game-world result below comes from the user. Since
  then the user runs all D2R tests; the agent no longer starts the game.
- Tested in D2R on the headset (SteamVR runtime, user, 2026-10-23): no crash, the
  log says the game brought its own ReShade, our panel opens with L3 + R3 and
  lists the mod's techniques, and switching user-added effects on and off works.
  The mod itself keeps `D2R_DepthFog` and `D2R_DepthFog_R` on, so switching
  those off in the panel does not stick; that is the mod's choice.
- Effects in subfolders (for example `SweetFX\FakeHDR.fx`) did not show up,
  because the mod's `ReShade.ini` lists `.\reshade-shaders\Shaders` without
  `\**`. The panel now offers `Load subfolders` for this (see "Effects in
  subfolders" in [DEVELOPMENT.md](DEVELOPMENT.md)). Tested in the simulator: the
  setting is saved and the effect loads without a restart.
- First D2R try: the button was greyed out and Save was always amber. The mod
  writes `D2R_DepthFog.fx` values (fog start, end and colour, plus a timer)
  every frame, so the preset never looked saved. Fixed: values the game keeps
  writing no longer count as unsaved changes, timer-style values are hidden,
  and the button stays usable with unsaved changes (`Load anyway`, with a
  warning). D2R retest (user, 2026-10-23): works; FakeHDR loads. The reload left all effects off for about 10 to 30 seconds with little feedback, so the panel now shows a seconds counter, a moving bar and a "ReShade is loading effects" message in the empty list. Retest (user, 2026-10-23): works; the reload took under a second, because ReShade had already built and cached these effects the first time.

## Suggested Fix

Goal: no list of games to maintain. Preferred: the add-on works together with the
game's own ReShade. If that proves too hard, it notices the game's own ReShade by
itself and stays out of the way.

### Option A (preferred): Work Together With The Game's ReShade

The mod draws each eye through the game's normal window picture (its ReShade log
shows the window resized to one eye, 3976x3924), so the mod's ReShade already
applies its effects to the headset image. Our panel could control that ReShade
instead of starting its own.

1. Detect the game's own ReShade (Option B, step 1).
2. Do not start the effect fallback. A second effect runtime on top of the mod's is
   what crashes the game.
3. Do not switch off effects on the "desktop" ReShade runtime. The add-on normally
   pauses desktop effects while the headset has its own runtime; here the desktop
   runtime is the one the mod uses for its fog and sky, so pausing it would remove them.
4. Point the panel at the mod's ReShade runtime, so its technique list, presets and
   settings are the mod's.
5. Keep the OpenXR panel and controller input, and check with the mod that our
   controller action set does not take input from the mod's own controls.

Risk: the panel changes the mod's preset (`D2R_Frame_ReShade.ini`) and the mod also
writes ReShade settings itself. This needs a headset test in the game.

### Option B (fallback): Detect And Step Aside

1. **Detect a game's own ReShade.** When the OpenXR layer starts, find the ReShade
   module in the process (the module that exports `ReShadeRegisterAddon`). Treat it
   as the game's own when it is not the central ReShade the OpenXR layer uses
   (for example not under `%ProgramData%\ReShade`, or not loaded as the
   `XR_APILAYER_reshade` layer). The repository's test scripts load ReShade from
   other folders, so the exact rule must keep those tests working (for example,
   tests can opt in through an environment variable).
2. **Stay a pass-through layer in that case.** Load and negotiate normally, so the
   OpenXR loader still counts a successful layer and problem 2 cannot happen. But
   do not register with ReShade, do not add the panel and do not start the effect
   fallback: `xrCreateApiLayerInstance` only calls the next layer, and
   `xrGetInstanceProcAddr` hands out the next layer's functions directly.
3. **Never start the effect fallback inside someone else's ReShade.** Even without
   step 1, the fallback should only start when the ReShade it would use is the
   central one. This alone stops the crash.
4. **Log one line** when the add-on stays out of the way, so users and the mod
   author can see why the panel is missing.

Problem 2 belongs to the OpenXR loader and the central ReShade layer. It could be
reported upstream (Khronos OpenXR-SDK loader, or ReShade so its layer loads and
passes through instead of refusing to load). Both options keep our layer loaded,
which protects our users either way.
