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
- Never use the word "harness" in Markdown files or answers; say "test script" or "test setup" instead.
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
- Every commit message the agent writes for the user must be a Conventional Commit (`docs: ...`, `fix: ...` and so on), following [Commit Messages](#commit-messages). A message without a type prefix is wrong, because the release changelog leaves it out.
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
- Never start a game, a game launcher, a mod launcher or an injector, not even with the OpenXR Simulator. The only programs the agent may start are the OpenXR Simulator test programs that `smoke.ps1` and `capture_simulator_ui.ps1` run. The user runs every game test (UEVR, SOMA-VR, D2R-PCVR and any other); the agent prepares the build, names the exact command or `.bat` to run and what to check, then reads the results.
- A per-game workaround is not an implementation of global add-on activation and must not be reported as a fix. Solve activation in this repository's add-on, installer, or supported machine-wide registration while preserving the no-loader and no-injection architecture.
- After a user-controlled game test, inspect game-local logs and configuration read-only. Ask the user only for headset observations that cannot be obtained from artifacts.

## UEVR Tests (User Runs Them)

- The agent never runs `tools/test_uevr_simulator.ps1 -Action Run` or `-Action Stop`, and never starts the game or the UEVR injector. The bullets below describe what the script does when the user runs it, and what the agent checks afterwards.

- Current scope: get ReShade effects into VR for Unreal games run through UEVR, per `tools/reshade-openxr-ui/UEVR_RESHADE_PLAN.md` (findings in `UEVR_RESHADE_RESEARCH.md`). Keep both files short and current.
- Approved test game: Creatures of Ava (Steam app 2304440, `A:\SteamLibrary\steamapps\common\Ava`). No other game may be launched for UEVR tests.
- The user launches it only through `tools/test_uevr_simulator.ps1 -Action Run`. It starts the game executable directly with `SteamAppId`/`SteamGameId` set, which stops Steam relaunching the game and showing the "custom arguments" prompt, and keeps the simulator setting in the game process.
- UEVR runs from `A:\UEVR-build\uevr-reshade-testing` (latest nightly). `-Action Deploy` overwrites its UEVR backend with the `.deps\UEVR` build; nothing else may write there. The user's UEVR copy under `%APPDATA%\UnrealVRMod\UEVR-build` is read-only reference.
- Select the OpenXR Simulator per process only, through a generated runtime manifest with an absolute DLL path (the bundle's own manifest fails with UEVR's older loader, error -51). Never change the machine-wide OpenXR runtime. `tools/disable_openxr_simulator.ps1` lets the user confirm or restore their headset runtime.
- `-Action Run` deletes the game's UEVR log (`%APPDATA%\UnrealVRMod\<game>\log.txt`) before starting, so the log and its copy in `build\uevr-simulator\<game>` hold only the latest run.
- Every wait step has a 30 second limit. If the expected result is not seen in time, kill every process the run started. On success, close the game and the injector.
- For live checks the user runs `-Action Run -KeepRunning` and closes the game with `-Action Stop`.
- The user's UEVR MCP plugin (`uevr_mcp.dll`, already in the game's UEVR plugins folder) answers at `http://localhost:8899/api` while the user's game runs. Use it for UEVR status, VR status, render details and desktop screenshots. Never use its game-launch or setup tools.
- After the user's run, verify it yourself: the script output, the saved headset capture (inspect it with the image viewer), and the UEVR and simulator logs in `build\uevr-simulator\<game>`. The OpenXR Simulator MCP server (`.vscode/mcp.json`) offers the same captures and logs when it is running.

## SOMA-VR OpenGL Tests (User Runs Them)

- The agent never runs `tools/test_soma_vr.ps1 -Action Run` or `-Action Stop`, and never starts SOMA or `Launch-SOMA-VR.ps1` (it also needs administrator approval). The bullets below describe what the script does when the user runs it, and what the agent checks afterwards.

- Current scope: OpenGL support (ReShade effects and the headset menu) for OpenXR games, tested with the SOMA-VR mod. Record findings and the plan in `tools/reshade-openxr-ui/OPENGL_SUPPORT.md` and keep it short and current.
- Approved test game: SOMA (`A:\SteamLibrary\steamapps\common\SOMA`) with the SOMA-VR mod already installed there. The mod source is in `.deps\SOMA-VR` (read-only reference).
- The user launches it only through `tools/test_soma_vr.ps1 -Action Run`, which calls the mod's own launcher (`Launch-SOMA-VR.ps1` in the game folder) with per-process settings: the build's add-on layer instead of the installed one, a repo-owned ReShade base folder (`build\soma-vr\reshade-base`), and by default the OpenXR Simulator. The launcher injects the mod and writes its own compatibility report; that is expected. Close with `-Action Stop`, which uses `Launch-SOMA-VR.ps1 -StopOnly`.
- The game folder stays read-only evidence for everything else: never edit, add or remove files there, including ReShade files and mod settings.
- Never change the machine-wide OpenXR runtime. `-UseActiveRuntime` runs on the user's headset runtime instead of the simulator.
- Every wait step in the script has a 30 second limit. For live checks the user runs `-Action Run -KeepRunning` and closes the game with `-Action Stop`.
- To request a test: build first, then tell the user the exact command and list what to check in the headset.
- After the user's run, verify it yourself from `build\soma-vr\last-run`: the headset capture (inspect it with the image viewer), `ReShade.log` (includes the add-on log), `hpl3vr.log` and the simulator log. Ask the user only for headset observations that the artifacts cannot show.

## D2R-PCVR Tests (User Runs Them)

- Current scope: games that bring their own ReShade, tested with the D2R-PCVR mod. Record findings in `tools/reshade-openxr-ui/MOD_OWN_RESHADE_CONFLICT.md` and keep it short and current.
- Test game: Diablo II: Resurrected (Battle.net edition, `C:\Progs\Diablo II Resurrected`) with the D2R-PCVR mod already installed in `mods\D2R-PCVR`. The mod source is in `.deps\d2r-pcvr` (read-only reference).
- Never launch D2R, `D2RLoader.exe` or `tools/test_d2r_pcvr.ps1 -Action Run`. Reaching the game world needs clicks in the game window (focus the window, press a key, click Play), which the agent cannot do, and a run that stops at the start screen tests nothing that matters. The user runs every D2R test.
- To request a test: run `build.bat`, then tell the user to double-click `test_d2r_pcvr.bat` in the repository root (`simulator`, `noaddon`, `stop` and `status` are its options), and list exactly what to check in the headset.
- After the user's test, read the logs read-only: the mod's `ReShade.log` (includes the add-on log), `d2r-vr-frame.log`, `d2r-pcvr-vrcam.log`. `tools/test_d2r_pcvr.ps1 -Action Status` (it starts nothing) shows whether the game is still running.
- The game folder is read-only evidence: never edit, add or remove files there, including the mod's settings (`d2r_pcvr.ini`), its ReShade files and its plugin DLL, unless the user asks for that exact change.
- Never change the machine-wide OpenXR runtime.

## Scripts The User Runs

- Everything the user runs is a `.bat` file in the repository root, next to `build.bat`. Never put a `.bat` in `tools` or another subfolder.
- Never ask the user to type a PowerShell command line or remember script parameters. If a task needs a user-run step and no root `.bat` exists for it, add one that calls the `.ps1` in `tools` and takes at most one or two plain words (for example `uninstall` or `status`) instead of parameters.
- When telling the user what to run, name the root `.bat` and its plain-word options only.

## Keep Findings Documented

- Write down every finding the user would otherwise have to remember (setup steps, settings that make something work, side effects such as settings an add-on leaves in a game folder) in the user-facing guide it belongs to (`INSTALL.md`, `INSTALL-SteamVR.md`, `LIMITATIONS.md`) in the same task, not only in developer notes.
- Before answering a question about how something works, read the existing docs, and answer from what was already found. Do not reason it out again from scratch if not needed.
- When the user asks to fix the instructions or docs, change the files in the same turn and say which lines changed.
- Instructions state general rules. Never write a rule or example around the specific case that prompted it (a particular commit, file, picture or bug), not even reworded; describe the general pattern so it applies to every similar case. When adding a rule, add no example unless the rule cannot be understood without one.
- When the user asks for something to be its own thing (its own guide, file or download), make it a separate file in that same turn. Don't fold it into an existing file as a section.
- Each install guide covers only its own case and never links to the other guides. Choosing the right guide happens only in the "Which download do I need?" table in `RELEASE_NOTES.md` and in the README files.

## Commit Messages

- Every commit message given to the user, with no exceptions, is a Conventional Commit: `type(optional scope): short plain-English summary`, for example `fix(opengl): stop the headset menu flickering in SOMA`. Before replying, check that the first line starts with one of the types below.
- Types: `feat` (new feature users notice), `fix` (bug fix), `perf` (faster or lighter), `revert`, `docs`, `refactor`, `build`, `ci`, `test`, `style`, `chore`. Add `!` after the type (`feat!:`) for a change that breaks existing installs or settings.
- Only `feat`, `fix`, `perf`, `revert` and `!` commits trigger a GitHub Release; all conventional commits appear in its changelog, and other messages are left out. So the summary must make sense to someone reading the release page.
- Write the summary line for players, not developers. Say what they notice or get, in everyday words, not how it was done: `fix: download links on the release page work again`, not `fix: download names without brackets`. Avoid file names, code names and technical terms in it. Only the summary line reaches the release page; put technical details in the body.
- The summary line must name the concrete thing that was added, changed or removed, and where, not only the goal behind it. When one commit contains several changes, the summary line names each of them, not only the main one.
- Adding `(Skip)` anywhere in the message stops GitHub Actions from building or releasing that push. Suggest it only when the commit changes nothing that is built or packaged (for example agent instructions or developer notes), and say so to the user.
- Give one commit message per logical change; if the work mixes unrelated changes, suggest separate commits.

## Edit And Validation Discipline

- Any failing build, test or validation is the agent's job to fix in the same task, whoever or whichever change caused it. Never report a failure as "pre-existing" or "not from this change" as a reason to leave it; find the cause, fix it, and rerun until everything passes. Only ask the user first when the fix needs a decision only they can make.
- Never implement or suggest workarounds. Find the root cause of a problem and fix it there, so the situation cannot happen in the first place. Code that detects a bad situation and hides or tolerates it (for example, making a second copy switch itself off) is a workaround, not a fix.
- Never state a cause as fact until a test proves it. If a change does not make the failing test pass, say plainly that the cause is still unknown.
- Use `apply_patch` for edits to existing files and keep patches small.
- After the first substantive edit, run the narrowest executable validation available before unrelated exploration or edits.
- Optimize for speed while implementing: iterate with the fast loop and run the slow full suite only once, at the end.
  - Fast loop for OpenXR add-on changes: `tools\reshade-openxr-ui\preview_ui.ps1` for anything visible (renders every menu page to PNG in about 5 s, no window), and `tools\reshade-openxr-ui\build.ps1` for behavior (builds the add-on and runs the renderer tests, about 40 s).
  - Never rerun `build.bat`, the smoke test or a full screenshot run between small edits; each full screenshot run takes several minutes per graphics API.
  - Final validation, once, before reporting done: `build.bat` from the repository root, then the full checks in `.github/instructions/openxr-ui-workflow.instructions.md`. Fix every failure and rerun only the failing step, then `build.bat` once more if code changed. Do not leave the build for the user to run.
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
