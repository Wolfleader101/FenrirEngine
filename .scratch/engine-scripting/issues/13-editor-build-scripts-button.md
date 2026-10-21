# 13: Editor "Build scripts" button

**What to build:** To be decided. In v1, scripts are built externally and the engine only watches the output (spec, Hot reload). This ticket adds an editor action that runs `dotnet build` on the project's script project, shows the build output and errors in the editor, and lets the watcher (10) pick up the result. Open questions: where the script project lives relative to `.feproj`, how build errors are surfaced, and how this fits the editor/runtime split, which doesn't exist yet.

**Blocked by:** none (hot reload was resolved and removed in 578fdd19)

**Status:** needs-triage

## Comments

Filed from the ticket 19 grilling session (Q4). The editor is still in development, so this waits on its structure.
