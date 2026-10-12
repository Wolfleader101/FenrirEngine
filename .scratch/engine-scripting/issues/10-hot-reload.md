# 10: Hot reload

**What to build:** Watch the script assembly's output directory. Once a new build is stable (debounced, with the file complete), reload between frames:

1. Copy the output to a fresh per-generation directory. Windows locks loaded assemblies (review-01 D5), so the build output itself is never loaded.
2. Run the old systems' `OnDestroy` and release the old generation, then confirm the unload (Iris 23). If a root leaked, log a warning that names it where possible.
3. Load the new generation all-or-nothing (05).
4. If the new generation fails, reload the previous copy and log why.

C# component pools and C# event queues survive when their full name and layout fingerprint (size, plus each field's name, offset and kind) are unchanged. Otherwise they are dropped and a warning is logged. System instances are always recreated.

**Blocked by:** 05, 08, 09, Iris 23

**Status:** ready-for-agent

- [ ] Changing a system's code and rebuilding changes behaviour without a restart, and component values survive.
- [ ] Changing a C# component's layout drops it from every entity, with a warning, and nothing reads stale bytes.
- [ ] A broken build (failed load) keeps the old generation running.
- [ ] Repeated reload cycles don't grow memory or leave generations `Pending`.
- [ ] Old per-generation copies are cleaned up once collected.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Hot reload).
