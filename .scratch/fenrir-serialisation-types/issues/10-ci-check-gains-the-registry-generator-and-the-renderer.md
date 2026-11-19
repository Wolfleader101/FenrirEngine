# 10: CI's dirty-tree check gains `fenrir-generate-type-registry` and runs with the renderer on; READMEs

**What to build:** a pull request that changes a header without regenerating its description, binding or type registration fails CI, for every described package including FenrirWindow and FenrirRenderer, whose descriptions have never been checked (ADR-0074 "CI's dirty-tree check gains the generator and runs with the renderer on"). The documentation names the third generated artefact wherever it names the first two.

- **The workflow:** `.github/workflows/api-descriptions.yml` configures with `FENRIR_ENABLE_RENDERER=ON` on its Windows runner (SDL3 configures there without a display; the comment that says the renderer is off because no package it builds has a description is replaced by one saying why it is on), regenerates descriptions, bindings and registrations in that order (`fenrir-describe-api`, `fenrir-generate-bindings`, `fenrir-generate-type-registry`), runs the registry generator's xunit tests beside the binding generator's, and fails on a dirty tree as today; its `paths-ignore` is unchanged.
- **The first run** regenerates FenrirWindow's and FenrirRenderer's descriptions, bindings and registrations for the first time under the check; whatever drift that surfaces is fixed in this ticket, since that is the check's point.
- **READMEs:** `README.md`'s description paragraph names the registration as the third committed artefact and the CMake target that regenerates it; `tools/describe-api/README.md` and `tools/generate-type-registry/README.md` point at each other and at the workflow; `packages/Iris/Tools/Iris.Bindings/README.md` names the roles and kinds it skips (ticket 04) and why; the "Building and testing" options table names nothing new (the generators run from targets, not options).
- **Local parity:** the three targets run from one `cmake --build` line a developer can copy from the README, in the order the workflow uses, so a dirty tree is found before the push.

**Blocked by:** 08, 09.

**Touches:** `.github/workflows/api-descriptions.yml`, `README.md`, `tools/describe-api/README.md`, `tools/generate-type-registry/README.md`, `packages/Iris/Tools/Iris.Bindings/README.md`, and any FenrirWindow or FenrirRenderer description, binding or registration the first renderer-on run regenerates

**Status:** ready-for-agent

- [ ] The workflow configures with the renderer on, runs the three generators in order and both generators' tests, and passes on the branch.
- [ ] A test commit that edits a described header in FenrirRenderer without regenerating fails the workflow, and the failure names the dirty files; the commit is dropped before merge.
- [ ] FenrirWindow's and FenrirRenderer's descriptions, bindings and registrations are byte-identical after the renderer-on regeneration, or regenerated and committed in this ticket.
- [ ] `README.md` and the three tool READMEs name the registration and the regeneration command; `grep -r feproj` and `grep -r "renderer is off"` find nothing stale.
