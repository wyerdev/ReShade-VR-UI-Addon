# ReShade VR UI Add-on Repository Instructions

## Repository Scope

- Rule 1: the add-on must never cause a frame-rate drop. Never make the game's frame wait on the GPU or the CPU (no `glFinish`, no per-frame fence or query waits, no blocking flushes) in any graphics API. When synchronization is unavoidable, use per-frame resources so a wait can only happen when the GPU is several frames behind, and measure the frame rate with and without the add-on on the headset before calling a change done.
- This is a standalone plugin repository. Preserve staged and unstaged user changes.
- The product must support both OpenXR and OpenVR. Do not remove or narrow either activation path without explicit user approval.
- Never reset, restore, delete, or overwrite worktree changes that were not made during the current task.
- Keep edits small, local, and consistent with the existing scripts and documentation.

## Plain-English Communication

- Always answer and explain things in plain English.
- Start with the direct answer or main conclusion before technical details.
- Do not assume the user understands programming terms, internal names, or acronyms.
- Avoid technical jargon when ordinary words communicate the same meaning.
- When a technical term is necessary, explain what it means in plain English the first time it appears.
- Explain problems as a concrete sequence of what happens, why it fails, and how the proposed change fixes it.
- Keep optional technical evidence separate from the main explanation.
- Do not use source-code names, log messages, or file references as a substitute for explaining their meaning.
- If the user says an explanation is unclear, restart from the basic facts with simpler language instead of repeating the same terminology.

## Git And Dependency Boundary

- This repository is owned and published independently from upstream ReShade.
- Never commit or push changes to the upstream ReShade repository as part of work here.
- The checkout under `.deps\reshade` is an external, ignored build dependency. Do not copy it into this repository or include it in release packages.
- `RESHADE-dependency.md` is an informational snapshot, not a pinned revision. Preserve user edits and regenerate it only through `setup-reshade.ps1` when the dependency checkout changes.
- Git is read-only for the agent. Only the user stages, unstages, commits, pushes, or otherwise changes Git state (branches, stash, reset, restore, remotes, config). The agent may run only read commands such as `git status`, `git diff`, `git log` and `git show`.
- The workspace hook `.github/hooks/git-read-only.json` blocks Git change commands and edits inside `.git` folders. Never disable, edit or work around it; `.github/hooks/test-git-read-only.ps1` checks it.
- `setup-reshade.ps1` changes the `.deps` Git checkouts, so run it only when the user asks.

## Privacy And Sensitive Data

- Never add or repeat usernames, account names, home-directory paths, secrets, API keys, tokens, credentials, private identifiers, or other sensitive personal information.
- Use placeholders, environment variables, and redacted values instead.

## ReShade Dependency Workflow

- Use `setup-reshade.ps1 -Action Setup` to clone the latest ReShade source and initialize submodules.
- Use `setup-reshade.ps1 -Action Update` to fetch and fast-forward the checkout's upstream default branch.
- Use `-ReShadePath` when building against an external ReShade checkout.
- Do not modify the external ReShade checkout to implement plugin behavior.

## Target Application Boundary

- Treat every game directory and its ReShade configuration, effects, presets, logs, DLLs, and manifests as read-only evidence. Never edit, generate, copy, delete, or replace files there unless the user explicitly requests that exact target-side change.
- Never modify a game-local `ReShade.ini` or `ReShadeVR.ini`, copy an add-on into a game directory, or change a game's launch configuration to make a test pass or to compensate for missing product behavior.
- Never launch a game on the user's behalf, except the approved test cycles below (UEVR and SOMA-VR). The user allows launching the games we work on with the OpenXR Simulator, selected per process only, but only when the agent can carry out the whole test itself; each such game still needs its own test cycle section and script here before it is launched. A game the agent cannot fully test (for example one that needs clicks in its window to reach gameplay) is tested by the user instead.
- A per-game workaround is not an implementation of global add-on activation and must not be reported as a fix. Solve activation in this repository's add-on, installer, or supported machine-wide registration while preserving the no-loader and no-injection architecture.
- After a user-controlled game test, inspect game-local logs and configuration read-only. Ask the user only for headset observations that cannot be obtained from artifacts.

## UEVR Test Cycle (Approved Exception)

- Current scope: get ReShade effects into VR for Unreal games run through UEVR, per `tools/reshade-openxr-ui/UEVR_RESHADE_PLAN.md` (findings in `UEVR_RESHADE_RESEARCH.md`). Keep both files short and current.
- Approved test game: Creatures of Ava (Steam app 2304440, `A:\SteamLibrary\steamapps\common\Ava`). No other game may be launched for UEVR tests.
- Launch it only through `tools/test_uevr_simulator.ps1 -Action Run`. It starts the game executable directly with `SteamAppId`/`SteamGameId` set, which stops Steam relaunching the game and showing the "custom arguments" prompt, and keeps the simulator setting in the game process.
- UEVR runs from `A:\UEVR-build\uevr-reshade-testing` (latest nightly). `-Action Deploy` overwrites its UEVR backend with the `.deps\UEVR` build; nothing else may write there. The user's UEVR copy under `%APPDATA%\UnrealVRMod\UEVR-build` is read-only reference.
- Select the OpenXR Simulator per process only, through a generated runtime manifest with an absolute DLL path (the bundle's own manifest fails with UEVR's older loader, error -51). Never change the machine-wide OpenXR runtime. `tools/disable_openxr_simulator.ps1` lets the user confirm or restore their headset runtime.
- `-Action Run` deletes the game's UEVR log (`%APPDATA%\UnrealVRMod\<game>\log.txt`) before starting, so the log and its copy in `build\uevr-simulator\<game>` hold only the latest run.
- Every wait step has a 30 second limit. If the expected result is not seen in time, kill every process the run started. On success, close the game and the injector.
- The 30 second limit is for waiting, not for testing. For live checks, use `-Action Run -KeepRunning`, finish every check while the game is running, then close it with `-Action Stop`. Never leave a session open at the end of a task.
- The user's UEVR MCP plugin (`uevr_mcp.dll`, already in the game's UEVR plugins folder) answers at `http://localhost:8899/api` while the game runs. Use it for UEVR status, VR status, render details and desktop screenshots. Never use its game-launch or setup tools.
- Verify each run yourself: the script output, the saved headset capture (inspect it with the image viewer), and the UEVR and simulator logs in `build\uevr-simulator\<game>`. The OpenXR Simulator MCP server (`.vscode/mcp.json`) offers the same captures and logs when it is running.

## SOMA-VR OpenGL Test Cycle (Approved Exception)

- Current scope: OpenGL support (ReShade effects and the headset menu) for OpenXR games, tested with the SOMA-VR mod. Record findings and the plan in `tools/reshade-openxr-ui/OPENGL_SUPPORT.md` and keep it short and current.
- Approved test game: SOMA (`A:\SteamLibrary\steamapps\common\SOMA`) with the SOMA-VR mod already installed there. The mod source is in `.deps\SOMA-VR` (read-only reference).
- Launch it only through `tools/test_soma_vr.ps1 -Action Run`, which calls the mod's own launcher (`Launch-SOMA-VR.ps1` in the game folder) with per-process settings: the build's add-on layer instead of the installed one, a repo-owned ReShade base folder (`build\soma-vr\reshade-base`), and by default the OpenXR Simulator. The launcher injects the mod and writes its own compatibility report; that is expected. Close with `-Action Stop`, which uses `Launch-SOMA-VR.ps1 -StopOnly`.
- The game folder stays read-only evidence for everything else: never edit, add or remove files there, including ReShade files and mod settings.
- Never change the machine-wide OpenXR runtime. `-UseActiveRuntime` runs on the user's headset runtime instead of the simulator.
- Every wait step has a 30 second limit. If the expected result is not seen in time, stop the game. The limit is for waiting, not testing: use `-KeepRunning` for live checks, then close the game. Never leave it running at the end of a task.
- When a headset observation is needed, leave the game running with `-KeepRunning`, stop and ask the user in the chat message, then close the game after the answer. The user is always available; a question-tool reply saying otherwise is not a reason to skip the check.
- Verify each run yourself from `build\soma-vr\last-run`: the headset capture (inspect it with the image viewer), `ReShade.log` (includes the add-on log), `hpl3vr.log` and the simulator log. Ask the user only for headset observations that the artifacts cannot show.

## D2R-PCVR Tests (User Runs Them)

- Current scope: games that bring their own ReShade, tested with the D2R-PCVR mod. Record findings in `tools/reshade-openxr-ui/MOD_OWN_RESHADE_CONFLICT.md` and keep it short and current.
- Test game: Diablo II: Resurrected (Battle.net edition, `C:\Progs\Diablo II Resurrected`) with the D2R-PCVR mod already installed in `mods\D2R-PCVR`. The mod source is in `.deps\d2r-pcvr` (read-only reference).
- Never launch D2R, `D2RLoader.exe` or `tools/test_d2r_pcvr.ps1 -Action Run`. Reaching the game world needs clicks in the game window (focus the window, press a key, click Play), which the agent cannot do, and a run that stops at the start screen tests nothing that matters. The user runs every D2R test.
- To request a test: run `build.bat`, then give the user the exact command (a new PowerShell window: the per-process `XR_API_LAYER_PATH`, `XR_ENABLE_API_LAYERS`, `DISABLE_XR_APILAYER_reshade_vr_ui=1` and `XR_RUNTIME_JSON` settings, then `.\D2RLoader.exe -mod D2R-PCVR` from the game folder) or `tools/test_d2r_pcvr.ps1 -Action Run -KeepRunning` for the user to run, and list exactly what to check in the headset.
- After the user's test, read the logs read-only: the mod's `ReShade.log` (includes the add-on log), `d2r-vr-frame.log`, `d2r-pcvr-vrcam.log`. `tools/test_d2r_pcvr.ps1 -Action Status` (it starts nothing) shows whether the game is still running.
- The game folder is read-only evidence: never edit, add or remove files there, including the mod's settings (`d2r_pcvr.ini`), its ReShade files and its plugin DLL, unless the user asks for that exact change.
- Never change the machine-wide OpenXR runtime.

## Edit And Validation Discipline

- Use `apply_patch` for edits to existing files and keep patches small.
- After the first substantive edit, run the narrowest executable validation available before unrelated exploration or edits.
- After changing source code or tests, run `build.bat` from the repository root yourself. Inspect and fix build or test failures, then rerun it until it passes; do not leave the build for the user to run.
- Finish with an executable validation whenever the environment provides one. Report unavailable or blocked validation clearly.

## Clean Code And File Organization

- Keep platform/API ownership, rendering, domain state, and tests in separate modules where those boundaries already exist.
- Prefer descriptive names, narrow scopes, early exits, RAII, and immutable data where practical.
- Comments should explain non-obvious constraints or decisions, not restate code.
- Do not add author attribution, copyright lines, SPDX tags, or per-file license blocks unless explicitly requested.

## Status And Continuation

- Always write the complete answer in the final message the user sees: what was found, what changed, the validation results, and anything the user must do. Never say "see the summary above" or leave results only in earlier messages, tool calls, or internal reasoning; the user may not see those.
- Never end a turn without visible answer text. Write the answer as plain message text after the last tool call, and also put the full answer in the task-complete summary, because text sent together with a tool call can be hidden.
- End every final response or pause with explicit `Status:` and `Next:` lines.
- `Status:` must identify what is complete, running, failed, or blocked and include the latest material validation result.
- `Next:` must name the exact continuation command, check, user action, or decision. Use `Next: None; task complete.` when finished.
