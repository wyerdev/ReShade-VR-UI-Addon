---
description: "Use when writing or suggesting a commit message: Conventional Commit format, player-facing summary line, (Skip), and the pre-commit file check."
applyTo: "**"
---

# Commit Messages

- Before suggesting any commit, list the staged and unstaged files (`git status --short`) and confirm none of them is another project's code, a change to it, or a file that only serves another project. Report anything doubtful to the user instead of suggesting the commit.
- Every commit message given to the user, with no exceptions, is a Conventional Commit: `type(optional scope): short plain-English summary`, for example `fix(opengl): stop the headset menu flickering in SOMA`. Before replying, check that the first line starts with one of the types below.
- Types: `feat` (new feature users notice), `fix` (bug fix), `perf` (faster or lighter), `revert`, `docs`, `refactor`, `build`, `ci`, `test`, `style`, `chore`. Add `!` after the type (`feat!:`) for a change that breaks existing installs or settings.
- Every push to `main` publishes a GitHub Release unless the summary line (first line) of the last commit ends with `(Skip)` or every type line in it is `docs`. All type lines except `docs` appear in the release notes; other lines are left out. So every type line must make sense to someone reading the release page.
- Suggest one commit for everything `git status --short` lists, including changes from earlier tasks that are still uncommitted (read their diff to describe them). Write one block per change: a `type(optional scope): summary` line, a blank line, its description, and a blank line before the next block. The most important change comes first. Each type line becomes its own release-notes entry; a description line must never start with a type and a colon.
- Write every type line for players, not developers. Say what they notice or get, in everyday words, not how it was done: `fix: download links on the release page work again`, not `fix: download names without brackets`. Avoid file names, code names and technical terms in it, and name the concrete thing that was added, changed or removed, and where, not only the goal behind it.
- Add `(Skip)` at the end of the summary line only when nothing in the commit is built or packaged and not every type line is `docs` (for example a `chore` that changes only agent instructions), and say so to the user. `(Skip)` in the body has no effect, and the release notes leave it out.
