---
description: "Use when writing or suggesting a commit message: Conventional Commit format, player-facing summary line, (Skip), and the pre-commit file check."
applyTo: "**"
---

# Commit Messages

- Before suggesting any commit, list the staged and unstaged files (`git status --short`) and confirm none of them is another project's code, a change to it, or a file that only serves another project. Report anything doubtful to the user instead of suggesting the commit.
- Every commit message given to the user, with no exceptions, is a Conventional Commit: `type(optional scope): short plain-English summary`, for example `fix(opengl): stop the headset menu flickering in SOMA`. Before replying, check that the first line starts with one of the types below.
- Types: `feat` (new feature users notice), `fix` (bug fix), `perf` (faster or lighter), `revert`, `docs`, `refactor`, `build`, `ci`, `test`, `style`, `chore`. Add `!` after the type (`feat!:`) for a change that breaks existing installs or settings.
- Every push to `main` publishes a GitHub Release unless the summary line (first line) of the last commit message contains `(Skip)`; all conventional commits appear in its changelog, and other messages are left out. So the summary must make sense to someone reading the release page.
- Write the summary line for players, not developers. Say what they notice or get, in everyday words, not how it was done: `fix: download links on the release page work again`, not `fix: download names without brackets`. Avoid file names, code names and technical terms in it. Only the summary line reaches the release page; put technical details in the body.
- The summary line must name the concrete thing that was added, changed or removed, and where, not only the goal behind it. When one commit contains several changes, the summary line names each of them, not only the main one.
- Adding `(Skip)` to the summary line (first line) stops GitHub Actions from building or releasing that push; `(Skip)` in the body has no effect. Suggest it only when the commit changes nothing that is built or packaged (for example agent instructions or developer notes), and say so to the user.
- Give one commit message per logical change; if the work mixes unrelated changes, suggest separate commits. Each message covers only the changes made in this task, never unrelated user changes.
