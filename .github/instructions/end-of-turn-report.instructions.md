---
description: "Use at the end of every turn: the fixed end-of-turn status report format, sorted by who owes the next action."
applyTo: "**"
---

# End-Of-Turn Report

End every final response or pause with a status report the user can read in five seconds. It must stand alone: assume the user reads only this report and never scrolls up.

## Sections

Sort every item into these headers, in this order, and leave out empty ones:

- `## 🔴 BROKEN`: something is broken and needs attention.
- `## ▶️ IN PROGRESS`: the agent owns the next step and is still going (for example a build or game test running in the background).
- `## ✅ DONE`: finished, nothing left to do. Include the latest validation result (for example "`build.bat` passed").
- `## 🛰️ DISPATCHED`: handed to something that will finish on its own, like a background agent or job.
- `## 📋 QUEUED`: written down, but nothing will pick it up yet.
- `## ⏳ PENDING`: waiting on the user to do something, such as a headset check or a commit.
- `## ❓ DECISION`: waiting on the user to choose.

PENDING and DECISION are always last. When nothing is broken, running, queued, pending or undecided, end with `## 🏁 ALL FINISHED`.

## Commit Message

Before writing the report, run `git status --short`. Only when it lists changes ready to commit does PENDING get a commit item, with the commit message (written as [commit-messages.instructions.md](commit-messages.instructions.md) describes) in a fenced code block. When it lists nothing, give no commit item and no commit message.

## Sort By Who Acts Next

- If the agent can do it without the user, do it now instead of ending the turn; list it under IN PROGRESS only if it is still running.
- If the user must act, put it under PENDING with the exact action and every fact needed to do it.
- If the user must choose, put it under DECISION and also ask with the question tool, with the deciding facts in the question.

## Item Shape

- Each item is a **bold title** on its own line, then one plain sentence. If it needs two sentences, make it two items. The only extra content allowed is a copyable code block, such as the commit message.
- At most three items per section, except DONE (at most six); merge smaller items.
- Say what the thing is and why it matters. Put IDs, file names and commands after the description, in parentheses. Explain any jargon in plain English.
- Say each thing once: an item appears in one section only, the commit message appears only in its PENDING item, and the report itself appears once per turn.
- Never write bare session-local names or soft asks like "want me to…".
- A short plain-English answer to the user's question may come before the first header; nothing comes after the last item.
