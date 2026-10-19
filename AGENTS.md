## Agent skills

### Issue tracker

Issues and specs are local markdown files under `.scratch/<feature>/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five-role vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.

## Build and test

Commands, options and submodules: the "Building and testing" section of `README.md`. Once per clone, `git config core.hooksPath .githooks` turns on the clang-format pre-commit hook.

## Standards

Rules for review: `CODING_STANDARDS.md`.

## Shell on Windows

Write multi-line files with the Write tool, not Bash heredocs: apostrophes and backslashes in them mis-parse, and `cat >` with no input hangs the call.

For `git show <rev>:<path>` in Bash, prefix `MSYS_NO_PATHCONV=1` (or use PowerShell): Git Bash otherwise rewrites the `rev:path` argument as a Windows path.
