# Handover: rebuild the VR menu from the sketch

The approved layout is [index.html](./index.html). Open it in a browser; point at buttons to see their
hover help. This plan turns it into the real in-headset menu (Dear ImGui) without changing what any
feature does.

## Rules that apply to every step

- No frame-rate cost: same ImGui draw lists, no animation, no GPU waits. Any new text size goes into
  `prebake_fonts`.
- Colours only from `vr_ui_theme.hpp` (`colors().…`); spacing on the 4/8 px grid; see
  `.github/skills/vr-ui-design/SKILL.md`.
- Every feature that exists today stays reachable. Only placement and names change.
- Settings keep their saved names (`[UI] PresetGesture`, `[UI] Theme`, `[GENERAL] PerformanceMode`, …).
- Keep the button IDs tests use (`move_up`, `hide`, `open`, `save`, `active_first`, …) where the button
  still exists.
- OpenXR and OpenVR must both keep working. The SteamVR overlay page (`openvr_ui_overlay.cpp`) is out
  of scope.
- Git is read-only for the agent; the user commits.

## Step 0: bug fix first (also visible in the headset today)

Disabled buttons get a darker background. The up arrow on the first effect and the down arrow on the
last effect therefore look different from the other arrows.

- Cause: `button_colors_for` in `openxr_ui_renderer.cpp` returns `colors().surface` as the background
  for disabled neutral buttons.
- Fix: keep the normal button background (`surface_raised`) and grey out only the icon or text
  (`text_disabled`).
- Check: `preview_ui.ps1`. Every arrow column must have the same background, with only the arrow greyed.

## Step 1: shared pieces (helpers)

Add these to `openxr_ui_renderer.cpp`, next to `draw_text_button`:

- **Switch**: rounded track plus knob. Accent when on, muted when off. Replaces tick boxes.
- **Segmented selector**: one inset strip with equal buttons; the chosen one is raised. Used for
  All / Starred / Hidden, with counts in quiet text.
- **Tab bar**: same look as the segmented selector, but larger and bold.
- **Settings row**: name on the left, control on the right edge; one line, at least 44 px tall.
- **Group card**: small uppercase title above a raised card.
- **Hover help**: one helper that shows a tooltip after hovering a control (reuse `draw_hover_details`).
  Every button that needs explaining gets one; labels stay short.
- **Icons**: draw the pin and "make taller" icons with lines, like the existing close and expand icons.
  The font has no emoji.

## Step 2: state

- Add the page: `effects`, `settings`, plus the existing effect page and Save As keyboard.
- Replace `show_favourites_only` and `show_hidden` with one list filter: `all`, `starred`, `hidden`.
- Keep the one-time "move active effects to top" command (`move_active_to_top`) unchanged. It must
  keep the order the user chose: active effects stay in the same order among themselves (an effect
  already on top stays first), and inactive effects keep their order below. Today this is
  `std::stable_partition` in `move_enabled_techniques_to_top` (`openxr_ui_model.hpp`), covered by the
  "stable ordering" check in `openxr_ui_renderer_test.cpp`. Never replace it with a sort.
- Add an "armed" flag for **Reset this effect** (the first press arms it, the second press resets).
  Clear the flag when the page changes or after a short time.

## Step 3: header (every page except Save As)

Left to right:

- Title "ReShade".
- Tabs **Effects | Settings / Gestures**.
- The **Effects on** switch. It replaces "Disable all" and "Restore shaders", with the same command.
- Icon buttons: pin ("Keep panel in place …"), make taller or shorter, and close. Each has hover help.

The effect page replaces the title and tabs with **‹ Effects**, the effect name and file, and a
**This effect on** switch.

## Step 4: Effects tab

- **Preset bar**: `Preset ‹ [name ▾] ›`, then "● Unsaved changes" (only when true), then on the right
  **Save** (accent only when unsaved), **Save as…**, and **Delete** (red text, neutral background).
- **List bar**:
  - The **All / Starred / Hidden** selector with counts.
  - Next to it, the **▲ Active on top** click button. It does the one-time sort (old "Active first").
    It is a button, not a switch, and it never changes the order of effects that are already on.
- **Notices**: the UEVR depth warning and the effect-subfolder notice stay as banners above the list.
- **Each row**:
  - The on/off switch.
  - The name in bold when on, with the file name in quiet text after it.
  - Then, in fixed columns from the right: ▲ ▼ group, star, **Hide** (or **Show** in the Hidden view),
    and **Open ›**.
  - Hover help on ▲ ▼ (or "Already at the top/bottom" when greyed), star, Hide and Open.
- **Bottom line** (quiet): "10 effects, 2 on" on the left; graphics API, resolution and version on the
  right. The version always shows in full.
- Remove the old footer settings from this page (they move to Step 5).

## Step 5: Settings / Gestures tab

Two columns of group cards:

- **Opening this menu**: Controller buttons (drop-down), Keyboard key (drop-down), Open when the game
  starts (switch).
- **Gestures**: a short paragraph saying what gestures do, then a row named **Next preset** with a
  drop-down:
  - Off.
  - Head Gesture: Left Trigger, Left Grip, Left X, Right Trigger, Right Grip or Right A.
  - L3+R3. Grey this out with "(already opens this menu)" when the controller buttons are plain L3+R3,
    the same rule as `vr_preset_gesture.hpp`.
- **FPS counter**:
  - Show when menu is closed (switch; was "Always show FPS").
  - **FPS graph target** drop-down with Full, Half and One-third headset rate. Hover help: it only
    changes which frames the graph marks as on time, not game speed.
- **Performance · needs game restart**:
  - **Faster shaders** (performance mode).
  - **Enable depth-based effects** (was "Depth buffer"; hover: off is faster in OpenGL games).
- **Look**: Colour theme.
- **Bottom line**: graphics info and add-on version.

## Step 6: effect page

- **Top bar**: the description (one line, full text on hover).
  - **Restore saved values** (was Revert/Restore).
  - **Reset this effect** (was Reset All; press twice to confirm).
- **Parameter rows**:
  - The name, with "● Changed" when the value differs from the preset.
  - Then the slider, the value, **−10× − + +10×**, and the ↺ reset.
  - On/off values show a switch instead of the slider and the "Toggle" button.
- **Bottom bar**: "Preset [name] ● Unsaved changes" and **Save preset** (accent).
- **Bottom line**: number of values, graphics, version.
- Show a scroll bar only when the values do not fit.

## Step 7: tests and preview

- `openxr_ui_renderer_test.cpp`: move every click position to the middle of the moved controls. Add
  tests for:
  - the tab switch;
  - each filter choice;
  - the one-time Active on top press;
  - the Effects on switch;
  - Reset this effect needing two presses;
  - the L3+R3 gesture being greyed out with the L3+R3 shortcut.
- `capture_simulator_ui.ps1`: update click positions; add a Settings capture.
- `openxr_ui_preview.cpp` / `preview_ui.ps1`: add a Settings page preview; keep the longest realistic
  header text (4320x4320 per eye).

## Step 8: documentation

Update control names in `README.md` and `DEVELOPMENT.md`:

| Old name | New name |
|---|---|
| Disable all | Effects on switch |
| Active first | Active on top |
| Starred only / Show hidden | Starred / Hidden filter |
| Revert | Restore saved values |
| Reset All | Reset this effect |
| Always show FPS | Show when menu is closed |
| Depth buffer | Enable depth-based effects |

## Step 9: validation (in this order)

1. While working: `tools\reshade-openxr-ui\preview_ui.ps1` after each layout change. View every page
   in every theme and compare with the sketch.
2. `tools\reshade-openxr-ui\build.ps1` after behaviour or test changes.
3. At the end:
   - `build.bat`.
   - `smoke.ps1 -TestRoot ..\OpenXR-Simulator-test -Frames 30` (D3D11 and D3D12, 30/30 frames, zero
     errors).
   - `capture_simulator_ui.ps1 -TestRoot ..\OpenXR-Simulator-test`, and view every capture.
4. Ask the user to run the SOMA-VR (OpenGL) check and a headset check. Compare the frame rate with
   and without the add-on.

## Done when

- Every page matches the sketch.
- The arrow backgrounds are all the same.
- Active on top is a one-time button that keeps the order of active effects.
- All old features are reachable.
- Tests, smoke and captures pass in both D3D11 and D3D12.
- The user has confirmed the headset check.
