---
description: "Use when writing or changing player-facing text: README, install guides, release page, limitations. Covers keeping it short, sourced claims only, plain wording, guide layout and formatting."
applyTo: "README.md,tools/reshade-openxr-ui/INSTALL*.md,tools/reshade-openxr-ui/LIMITATIONS.md,tools/reshade-openxr-ui/OPENXR-LAYERS.md,tools/reshade-openxr-ui/RELEASE_NOTES.md"
---

# Player Docs

## Keep It Short (Most Important)

- Players don't want to read. Use as few words as possible: only what a player needs to install, use or fix the add-on.
- Leave out intros, explanations of why, background, repeats and anything a player can do without. When in doubt, cut it.
- Short sentences, one point each. Prefer a short list or step over a paragraph.
- Before finishing, reread every changed line and cut every word that can go without losing meaning. Adding text must never make a page longer than needed; if something new goes in, look for something to cut.

## Only Write What Has A Source

- Every sentence about what the add-on does, or what the player sees, clicks or presses, must come from one of these: the committed player docs (the user has already checked them), the user's own words, a test run in the current task that showed it, or a feature built in the current task (describe it as finished and working, with the names shown in the menu).
- When a step or claim has no such source, ask the user before writing it. Never fill a gap with a guess, with general VR or ReShade knowledge, or with what "usually" happens.
- When the user confirms or corrects how something works, fix it in every player doc that mentions it in the same turn.
- Before finishing, reread every changed sentence and check each claim against its source. In the reply, list each new claim about behaviour or controls and say where it was verified.

## Wording

- Describe this add-on, not ReShade in general. Start with what the add-on does for the player.
- Everyday words: no invented terms, no developer words. One name for each thing, matching the names in the menu.
- Never word options as a choice by game type unless the choice really depends on the game.
- Bold the action words a player must get right (tick, untick and their forms) and the name **OpenXR**; leave ordinary words such as "layer" plain.

## Layout

- Each install guide starts with a short version of at most three steps, followed by the full steps in a `<details><summary><b>Step by step</b></summary>` fold-out.
- When there are several ways to install, show every way at the same level, each with its own heading and short steps. Never hide a way inside a fold-out or behind vague wording.
- Each install guide covers only its own case and never links to the other guides. Choosing the right guide happens only in the download table of `RELEASE_NOTES.md` and in the README files.
- When the user asks for something to be its own thing (its own guide, file or download), make it a separate file in that same turn. Don't fold it into an existing file as a section.
- The repository is private: guides use relative image paths; the release page uses `https://github.com/{{REPOSITORY}}/raw/{{TAG}}/...` for images and `/blob/{{TAG}}/...` for guide links. Never use `raw.githubusercontent.com`.
