---
name: vr-ui-design
description: 'Design or restyle the in-headset VR menu (Dear ImGui panel) so it looks modern, deliberate and readable instead of default or "AI slop". Use when changing colors, theme, fonts, typography, spacing, layout, buttons, sliders, icons, visual polish, or when the user says the UI looks ugly, dated, generic or hard to read in VR.'
argument-hint: 'What to restyle, for example "whole panel", "detail page", "buttons"'
---

# VR UI Design

Makes the add-on's headset panel look like one designed product: one committed look, one font, one color system, consistent spacing. The panel is Dear ImGui drawn into a texture that floats in VR, so web design advice only partly applies; the VR rules below win when they conflict.

## When to Use

- The user says the panel looks ugly, dated, cluttered, generic, or hard to read.
- Adding or changing any visible control, color, font, spacing or layout in `tools/reshade-openxr-ui/openxr_ui_renderer.cpp`, `vr_frame_graph.hpp` or the OpenVR overlay.
- Reviewing a UI change before calling it done.

## Hard Constraints

- **Drawing must not cost frame rate:** no per-frame font rebuilds, no extra render passes, no blur or shader effects, no continuous animation that forces the panel to redraw when nothing changed. Fonts load once at start-up.
- Changes must work identically in D3D11, D3D12 and OpenGL (they share the ImGui draw lists; only use ImDrawList primitives).
- Fonts must be embedded in the add-on binary (no files read from game folders) and must have a license that allows redistribution (SIL OFL or Apache 2.0). Record the license in `THIRD-PARTY-NOTICES.txt`.
- Keep every existing control, label meaning, and the IDs used by tests and `capture_simulator_ui.ps1`. Restyling must not change behavior.

## The "Slop" Fingerprint To Avoid

Generic output comes from the most common, safest choices. Do not ship any of these:

- ImGui's built-in pixel font (ProggyClean) or its default blue theme.
- Inter, Roboto, Arial or Segoe as the main font.
- Purple-to-blue gradients, glassy blur, neon glows, drop shadows on everything.
- A different color for every button ("rainbow buttons"); color used as decoration rather than meaning.
- Everything the same size and weight, so nothing stands out.
- Random spacing values (3, 5, 7, 11 px) instead of a scale.
- Color hard-coded at each call site (`IM_COL32(...)` sprinkled through the drawing code).

## VR-Specific Rules (override web advice)

The panel is seen through lenses at arm's length, on a low pixel density screen, and aimed at with a shaky controller laser.

- **Legibility first.** Use a font designed for low-vision readability (open shapes, distinct `Il1`, `0O`). Body text at least 20 px in panel space; secondary text at least 17 px. Prefer medium weight over light or thin.
- **Contrast.** Main text at least 7:1 against its background; secondary text at least 4.5:1. Lens blur eats contrast.
- **No pure black or pure white areas.** Pure black smears on OLED headsets; large pure white glares. Surfaces are tinted dark neutrals; brightest text is slightly off-white.
- **No hairlines or high-frequency detail.** 1 px lines and noise textures shimmer and crawl as the head moves. Strokes and borders at least 1.5 to 2 px; no textures, no dithering.
- **Big targets.** Every clickable control at least 30 px tall (34 px or more for new controls), with at least 6 px gap to its neighbours, so the laser does not hit the wrong one.
- **State never by color alone.** Pair color with a label, icon, fill or position change (color-blind users, and colors shift in some headsets).
- **Motion is comfort-critical.** The panel itself never moves, scales or slides. Feedback is an instant color change; optional short fades are fine only if they stop drawing when finished.
- **Calm edges.** Keep content away from the panel border; text near the edge of a lens is blurry.

## Design System

Every color comes from the active theme in `tools/reshade-openxr-ui/vr_ui_theme.hpp`. Drawing code calls `colors().accent`, `colors().surface_raised` and so on, never raw numbers; shape and type sizes are fixed constants there (`vr_theme::radius_sm`, `vr_theme::font_control`).

- **Themes are our own.** Never copy another project's palette or theme (no Catppuccin, Dracula, Nord and so on). Design new colors from the rules here. Each theme fills a `theme_source` (13 colors), and `make_palette` derives every panel color with one shared rule. Current themes: Violet Night (default), Plum and Slate.
- **Add a theme**: add its name to `vr_ui_themes.hpp`, then its `theme_source` in the same position in `vr_ui_theme.hpp`. Pick one surface hue and step it: background, darker inset, rows, controls, hovered controls, muted, subtext, text; then one accent, warning (yellow), success (green), danger (red), and text on accent (the background).
- Users pick a theme in the menu footer or with `Theme=<name>` under `[UI]` in the add-on's `settings.ini`; the panel and FPS counter switch together.
- **Colors must arrive exactly as chosen.** The panel swapchain is usually sRGB, so the GPU brightens every written color. The renderer converts vertex colors to linear first (`convert_draw_data_to_linear`) when the layer reports an sRGB target. Without this, dark themes look washed out and grey.
- The panel background is opaque, so the game never shows through.
- **Fonts are drawn once at start-up** (`prebake_fonts`) for every font and size used. When adding a new text size, add it there, or ImGui draws it on first use mid-game, which can repack the font image and make the D3D12 backend wait for the GPU.

Spacing and layout rules (the constants at the top of `openxr_ui_renderer.cpp`):

- **One spacing grid.** Every gap and size is a multiple of 4, mostly of 8: 4 inside rows, 8 between controls, 16 between sections, 24 around the panel. Never use one-off values such as 3, 6, 37 or 74. Professional interfaces use a fixed grid like this, not the golden ratio.
- **One control height**: 32 px for buttons, boxes and drop-down lists in headers and footers. List rows are at least 40 px with 6 px gaps; the number of rows that fit is computed from the space, never fixed.
- **One line per item.** Effect rows and setting rows keep everything on one line: name in a fixed-width column on the left, then the controls in fixed columns, so every row lines up and more items fit.
- **Rows of buttons have fixed widths from the right edge** (for example Open, Hide, star, down, up), so columns line up across rows.
- Long names end with "..." and show the full text on hover; descriptions get one line, with the full text on hover.
- **Never hide or shorten other elements to make room.** Adding or widening a control must not truncate, cover or push out anything already there. Version, page title, counts and graphics API are information the user relies on: they always show in full. If a line has to shorten, only its least important part shortens (for example the resolution), never the version. When a row is full, move the new control to a row with room instead of squeezing.
- **Leave the header breathing room.** The info line (count, graphics, version) must fit in full on the widest real headset text with clear space to spare, not just barely. When a header gets crowded, turn well-known actions into square icon buttons with a hover name (close, expand/collapse, arrows) before shortening any text.
- **Group related controls tightly; separate groups clearly.** Controls that belong together (FPS switch, Pin UI, expand icon, close) sit `space_sm` (8) apart; groups are `space_md` (16) or more apart. Size tick boxes and text buttons to their label plus padding (`tick_box_label_x + label width + space_sm`), never a guessed fixed width that leaves a visible gap inside a group.
- Controls that cannot do anything are hidden, not drawn empty (no slider track for an On/Off value, no scroll bar track when nothing scrolls).

Color rules:

- One dominant tinted dark neutral family for surfaces, stepped in clear lightness levels (background < surface < raised < hover).
- **One** accent hue, used for the primary action, focus, selection and slider fills. Nothing else.
- Meaning colors (`success`, `warning`, `danger`) appear only when that state is true (unsaved changes, failed save, destructive action), never as idle decoration.
- Ordinary buttons are neutral. Only the single primary action on a screen may be filled with the accent; destructive actions are neutral until hovered or armed, then `danger`.

Hierarchy rules:

- Three zones per page: header (title, context, close), content (list or parameters), footer (settings, status). Separate zones with space or a surface step, not boxes inside boxes.
- One clear title per page, larger than body text. Secondary info (counts, API, resolution, version) uses `text_secondary` and the small size.
- Align everything to a shared left edge and a consistent column grid; right-align numbers.

## Procedure

1. **See the current state** with `preview_ui.ps1` (see Validation) and list what looks wrong against the slop fingerprint, the spacing rules and the VR rules.
2. **Colors**: change or add a theme (see Design System). Never hand-pick colors at a call site.
3. **Font**: Atkinson Hyperlegible Regular and Bold (SIL OFL) are embedded byte for byte by `embed_fonts.ps1` during the build; the license is in `fonts\OFL.txt` and `THIRD-PARTY-NOTICES.txt`. A replacement font must also be OFL or Apache 2.0 and get the same notice.
4. **Helpers before screens**: change `draw_text_button`, `draw_icon_button`, `draw_check_indicator`, sliders and drop-downs first, then individual screens (header, list rows, detail parameters, footer, Save As keyboard, toasts).
5. **Validate** after each change (see Validation).

## Review Checklist

- [ ] Real embedded font; no ProggyClean pixels anywhere.
- [ ] No raw `IM_COL32` in drawing code; colors only from `colors()` (frame-graph bar colors may stay because they encode meaning).
- [ ] At most one accent hue visible at rest; meaning colors appear only for real state.
- [ ] Title clearly the largest text; secondary info visibly quieter.
- [ ] Every clickable control at least 30 px tall with a visible hover state; text never touches a control's edge.
- [ ] No 1 px lines, no see-through panel, no textures.
- [ ] Colors in the capture match the theme's palette (not lighter or greyer).
- [ ] Text never clipped or overlapping in any capture.
- [ ] Every element that was visible before the change is still visible and complete (compare before and after previews); the version shows in full.
- [ ] Related controls are grouped with 8 px gaps; no wide empty gap inside a group.
- [ ] Nothing animates continuously.

## Validation

Follow the fast loop and the final checks in `.github/instructions/openxr-ui-workflow.instructions.md`. For design work also:

- The sample effects and settings are in `openxr_ui_preview.cpp`. The preview uses the longest realistic header text (a 4320x4320 per-eye headset) so truncation shows up; never judge a layout only on short sample text. Before a layout change, render and keep the previews, then compare before and after.
- When OpenGL drawing changed, ask the user to run the SOMA-VR test.
