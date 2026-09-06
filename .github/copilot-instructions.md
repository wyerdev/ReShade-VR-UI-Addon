# ReShade VR UI Add-on Repository Instructions

## Repository Scope

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
- Do not initialize, commit, create remotes, or push unless the user explicitly requests that Git operation.

## Privacy And Sensitive Data

- Never add or repeat usernames, account names, home-directory paths, secrets, API keys, tokens, credentials, private identifiers, or other sensitive personal information.
- Use placeholders, environment variables, and redacted values instead.

## ReShade Dependency Workflow

- Use `setup-reshade.ps1 -Action Setup` to clone the latest ReShade source and initialize submodules.
- Use `setup-reshade.ps1 -Action Update` to fetch and fast-forward the checkout's upstream default branch.
- Use `build.bat` from the repository root for the normal build and package flow.
- Use `-ReShadePath` when building against an external ReShade checkout.
- Do not modify the external ReShade checkout to implement plugin behavior.

## Target Application Boundary

- Treat every game directory and its ReShade configuration, effects, presets, logs, DLLs, and manifests as read-only evidence. Never edit, generate, copy, delete, or replace files there unless the user explicitly requests that exact target-side change.
- Never modify a game-local `ReShade.ini` or `ReShadeVR.ini`, copy an add-on into a game directory, or change a game's launch configuration to make a test pass or to compensate for missing product behavior.
- A per-game workaround is not an implementation of global add-on activation and must not be reported as a fix. Solve activation in this repository's add-on, installer, or supported machine-wide registration while preserving the no-loader and no-injection architecture.
- After a user-controlled game test, inspect game-local logs and configuration read-only. Ask the user only for headset observations that cannot be obtained from artifacts.

## OpenXR Validation

- Use the isolated OpenXR Simulator bundle supplied through `-TestRoot`. Do not rebuild, install, register, or modify that bundle unless the task explicitly targets it.
- Never launch a game, deploy to Hubris, modify a runtime registration, or change machine-wide configuration as part of automated validation.
- WARP tests validate interaction and state transitions only. Visual acceptance requires inspecting every actual simulator capture produced by `capture_simulator_ui.ps1`.
- Inspect D3D11 and D3D12 captures when both APIs are requested. Do not treat logs, hashes, hit tests, or screenshot existence as visual proof.
- Preserve simulator configuration and preset bytes; temporary staged DLLs, manifests, local-app-data state, and test presets must be removed by the harness.

## Edit And Validation Discipline

- Use `apply_patch` for edits to existing files and keep patches small.
- Use Windows PowerShell commands and checked-in scripts. Do not assume a Unix shell is available.
- After the first substantive edit, run the narrowest executable validation available before unrelated exploration or edits.
- Finish with an executable validation whenever the environment provides one. Report unavailable or blocked validation clearly.

## Clean Code And File Organization

- Keep platform/API ownership, rendering, domain state, and tests in separate modules where those boundaries already exist.
- Prefer descriptive names, narrow scopes, early exits, RAII, and immutable data where practical.
- Comments should explain non-obvious constraints or decisions, not restate code.
- Do not add author attribution, copyright lines, SPDX tags, or per-file license blocks unless explicitly requested.

## Status And Continuation

- End every final response or pause with explicit `Status:` and `Next:` lines.
- `Status:` must identify what is complete, running, failed, or blocked and include the latest material validation result.
- `Next:` must name the exact continuation command, check, user action, or decision. Use `Next: None; task complete.` when finished.
