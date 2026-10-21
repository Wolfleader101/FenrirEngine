# Issue tracker: Local Markdown

Issues and specs for this repo live as markdown files in `.scratch/`.

## Conventions

- One feature per directory: `.scratch/<feature-slug>/`
- The spec is `.scratch/<feature-slug>/spec.md`
- Implementation issues are one file per ticket at `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01`, never a single combined tickets file
- Triage state is recorded as a `Status:` line near the top of each issue file (see `triage-labels.md` for the role strings). `scripts/check_tickets.py` checks every ticket's Status and Blocked by lines; the pre-commit hook and CI run it
- Comments and conversation history append to the bottom of the file under a `## Comments` heading

## Implementation tickets

- **Touches**: each implementation ticket has a `**Touches:**` line under `**Blocked by:**`, naming the files or directories it expects to edit (`examples/DemoApp/src/VehicleScene.hpp`, `packages/FenrirPhysics/src/`). The line is for scheduling, so it overrides `/to-tickets`' advice to leave paths out.
- **Stacking**: tickets whose Touches lines share a file are built as a **stack**. Each ticket's branch is cut from the previous ticket's pushed branch, and its PR targets that branch, so each PR shows only its own diff. Only tickets whose Touches lines are disjoint run side by side off the base. Settle the stack's order, respecting every Blocked by line, before starting the first ticket.
- **Built**: when a spec's last ticket merges, the commit that records it built deletes `.scratch/<feature-slug>/`. If a map charted the spec, its line names that commit, and git history keeps the files.

## When a skill says "publish to the issue tracker"

Create a new file under `.scratch/<feature-slug>/` (creating the directory if needed).

## When a skill says "fetch the relevant ticket"

Read the file at the referenced path. The user will normally pass the path or the issue number directly.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a file with one **child** file per ticket.

- **Map**: `.scratch/<effort>/map.md` (the Notes / Decisions-so-far / Fog body).
- **Child ticket**: `.scratch/<effort>/issues/NN-<slug>.md`, numbered from `01`, with the question in the body. A `Type:` line records the ticket type (`research`/`prototype`/`grilling`/`task`); a `Status:` line records `claimed`/`resolved`.
- **Blocking**: a `Blocked by: NN, NN` line near the top. A ticket is unblocked when every file it lists is `resolved`.
- **Frontier**: scan `.scratch/<effort>/issues/` for files that are open, unblocked, and unclaimed; first by number wins.
- **Claim**: set `Status: claimed` and save before any work.
- **Resolve**: append the answer under an `## Answer` heading, set `Status: resolved`, then append a context pointer (gist + link) to the map's Decisions-so-far in `map.md`.
- **Concurrent sessions**: each ticket session that runs alongside another works in its own git worktree, because one checkout has one git index and a commit there picks up whatever another session has staged. Its tracker edits go on its own branch, which is merged into the base afterwards.
  - Rebase the tracker commit onto the latest base just before committing, so it fast-forwards.
  - A conflict in the map's Decisions so far or Not yet specified keeps both sides' lines.
  - A ticket session working in the main checkout makes a branch before its first commit.
