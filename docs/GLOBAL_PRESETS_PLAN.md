# Global Presets And Shared Starred Effects: Plan For The Implementer

Status: done. Built as designed below; how it works now is described in `DEVELOPMENT.md` ("Global presets and shared starred effects").

## What The Player Wants

- **Share a preset between games.** Save the current preset as a global preset, then use it in another game, even if that game does not have the effects the preset needs.
- **Missing effects come along.** Importing a global preset copies every effect it needs that the game lacks, together with the helper files (`#include` files) and textures those effects use.
- **Each game gets its own copy.** Changing an imported preset in a game changes only that game's copy. The player can save it back as global, which makes a new version.
- **New versions are announced.** Other games that imported an older version show that a new version exists, and the player can choose to update.
- **Starred effects travel too.** Starring an effect also saves its files to the shared library, so other games can show it and add it with one click.
- **No double effects.** Nothing may ever make an effect load twice in a game, whether or not the game already has it installed.

## Why A Plain Shared Folder Does Not Work

ReShade loads every `.fx` file it finds in the folders listed under `EffectSearchPaths` and does not skip files with the same name (`find_files` in `.deps\reshade\source\runtime.cpp`). Presets name effects by file name only. So if the shared library folder were added to a game's effect folders, every effect the game already has would load twice and the preset could not tell the copies apart.

Therefore:

- The library folder is **never** added to `EffectSearchPaths` or `TextureSearchPaths`.
- Effects reach a game only by **copying the missing files** into the game's own effect and texture folders.

## Where Things Are Saved

- **Shared library:** `%LOCALAPPDATA%\ReShade VR UI Add-on\Global\` (the same base folder as `favourite-shaders.txt` and `settings.ini`).
  - `Presets\<name>.ini`: the preset in ReShade's normal preset format.
  - `Presets\<name>.txt` (or a similar small file next to it): version number and the list of files the preset needs, each with a content hash.
  - `Files\<hash>\<original file name>`: one folder per distinct file content, so two different versions of `Bloom.fx` from two games never overwrite each other. A preset's file list points at the exact versions it was saved with.
- **Per game:** the imported preset is a normal preset file in the game's preset folder. Which global preset and version it came from is recorded in the add-on's own per-game data, not inside the ReShade preset, so ReShade never sees unknown keys.

## How Each Action Works

### Save current as global

1. Ask for a name with the existing Save As keyboard; the default is the current preset's name.
2. If a global preset with that name exists, ask "Replace 'X' (v3 → v4)?".
3. Collect the needed files (see "Finding What An Effect Needs") and copy any file not already in the library into `Files\<hash>\`.
4. Write the preset and its file list with version 1, or the old version plus one.

### Save back to global

- Shown only when the current preset was imported from a global one.
- Same as "Save current as global" under the original name, without asking for a name; raises the version by one.

### Import (and Update)

1. For every file the preset needs, check by **file name** whether the game already has it (any `EffectSearchPaths` folder for effects and helper files, any `TextureSearchPaths` folder for textures, respecting `\**` for subfolders).
2. Files the game has are **never replaced**. Count the ones whose content differs from the library version; they are reported, not copied.
3. Copy only the missing files: effects and helper files into the first `EffectSearchPaths` folder, textures into the first `TextureSearchPaths` folder.
4. Copy the preset into the game's preset folder, record its source and version, switch to it and let ReShade reload effects once.
5. **Update** does the same but replaces this game's copy of the preset, so the confirmation box warns that changes made in this game are lost.

### Starring an effect

- Starring still writes `favourite-shaders.txt` as today, and now also copies the effect's files into the library in the background.
- The list of starred effects needs to remember which library files belong to each one, so another game can add it.
- In another game, starred effects the game does not have appear in the Starred list as greyed rows with an **Add** button. Add uses the same import steps and confirmation box, for a single effect.
- If the game later gets that effect installed by other means, the row becomes a normal row automatically, because rows are matched by effect file name and technique name.

## Finding What An Effect Needs

- Follow `#include "..."` lines recursively from the effect file. Look up each include next to the including file first, then in the effect folders, as ReShade does.
- Collect texture file names from `source = "..."` annotations in every collected file.
- Copy all of them, even ones used only behind a preprocessor switch, so changing a switch later still works.
- Known limit: a file name built by the preprocessor (for example from a macro) cannot be found this way. Log it and report the effect as possibly incomplete instead of guessing.

## UI Sketch

All drawing follows the `vr-ui-design` skill (`.github\skills\vr-ui-design\SKILL.md`): theme colors only, the 4/8 px spacing grid, 32 px controls, one accent for the primary action, nothing hidden or shortened to make room.

### New page: Global presets

```
┌─ Global presets ───────────────────── 3 shared · D3D11 · v1.x   [⤢][✕]┐
│  Current: Dark Cave (copy of global "Dark Cave" v3, changed here)     │
│  [ Save current as global ]   [ Save back to global → v4 ]            │
│                                                                       │
│  NAME            VER   EFFECTS              STATUS                    │
│  Dark Cave       v3    5                    ✓ In this game    [Load]  │
│  Bright Day      v3    4 · 1 missing        ↑ v3 new (have v2)[Update]│
│  Retro CRT       v1    3 · 3 missing        Not in this game[Import]  │
│                                                                       │
├───────────────────────────────────────────────────────────────────────┤
│  Effects | Settings | Gestures | Global presets          Theme ▾      │
└───────────────────────────────────────────────────────────────────────┘
```

- One row per global preset; the row count comes from the available space and the list scrolls, as on the Effects page.
- Status is never shown by color alone: each status has its own symbol and words.
- "Save back to global" is hidden when the current preset did not come from a global preset.

### Confirmation box (Import, Update, Add)

The game may have hundreds of effects installed, so the box shows **counts**, never a list of files the game already has. Lists appear only on hover, and only for the short groups.

```
┌─ Import "Retro CRT" into this game? ─────────────────────────┐
│  Adds 3 effects and 2 textures            (hover: names)     │
│  Uses 14 files this game already has                         │
│  2 of them are a different version        (hover: names)     │
│  Effects switch off for a few seconds while ReShade loads.   │
│  [ Import and load ]                         [ Cancel ]      │
└──────────────────────────────────────────────────────────────┘
```

- The "different version" line appears only when that count is above zero; it uses the warning color with its symbol.
- For **Update**, one more line: "Your changes to this preset in this game will be replaced." The action button stays neutral until pointed at, then turns red.
- Long hover lists end with "and N more".

### Starred list in a game without the effect

```
 All 42 | Starred 6 | Hidden 2
 ★ Clarity                [■ On]  ⋯   ▲▼
 ★ CRT       Not in this game          [Add]
```

## Where The Code Goes

Follow the module rules in `.github\copilot-instructions.md` ("Clean Code And File Organization").

- **New module `openxr_ui_global_library.hpp/.cpp`** in `tools\reshade-openxr-ui`: library folder layout, hashing, finding what an effect needs, comparing with the game's folders, copying files, preset versions and per-game source records. No module covers this today. It must not depend on any graphics API.
- **`openxr_ui_preset.*`**: saving the current preset to the library and switching to an imported copy, next to the existing Save As, switch and delete code.
- **`reshade_openxr_ui_addon.cpp`**: the state functions the menus call, declared in `vr_ui_state.hpp` (today's favourites live at `openxr_ui_set_technique_favourite` and `favourite_techniques_path`). Extend the starred-effect handling here.
- **`openxr_ui_model.hpp`**: snapshot data for the page (global preset rows with version and status) and for greyed starred rows.
- **`openxr_ui_renderer.*`**: the new page, the confirmation box and greyed starred rows, plus new `render_result` commands handled in `openxr_ui_layer.cpp`.
- **`openvr_ui_overlay.cpp`**: the same page and actions in the SteamVR menu. Both OpenXR and OpenVR must get the feature.
- **`openxr_ui_preview.cpp`**: sample global presets and a greyed starred row for the menu pictures.

## Rules That Must Hold

- **No frame-rate cost.** Hashing, scanning folders and copying run on a worker thread, never on the drawing thread and never per frame. The page reads the library only when it is opened or after an action finishes. Show "Working..." while a job runs.
- **Never replace a game's file.** Only missing files are copied; same-name files are always left alone.
- **Never load an effect twice.** The library folder is never a search folder.
- **Failures are reported, not hidden.** A failed copy stops the import before the preset switches, shows the reason in the panel and is logged. Files already copied by that import are removed again.
- **Game-brought ReShade.** Games whose own ReShade is used (see `openxr_ui_game_brought_own_reshade`) use that ReShade's folders the same way.
- **Licenses.** The add-on only copies the player's own files on their own PC and ships none, so no new third-party notices are needed.

## Tests And Finishing

- Add tests to `openxr_ui_renderer_test.cpp` for: the page commands (Save current as global, Save back, Import, Update, Load), the confirmation box counts and the hidden "different version" line, and the greyed starred row's Add command.
- Add tests for the library module using a temporary folder: include and texture discovery, skipping same-name files, version numbers, different-version counting, and cleanup after a failed copy.
- Run the fast loop and the final checks in `.github\instructions\openxr-ui-workflow.instructions.md`.
- Finish the docs in the same task: README feature list, `docs\INSTALL.md` (how to share a preset between games, where the library lives), `docs\INSTALL-SteamVR.md` if its menu differs, `docs\LIMITATIONS.md` (effects whose file names are built by the preprocessor), `docs\RELEASE_NOTES.md`, and new menu pictures for the Global presets page and the Starred list.
- Then change the status line at the top of this file to done.
