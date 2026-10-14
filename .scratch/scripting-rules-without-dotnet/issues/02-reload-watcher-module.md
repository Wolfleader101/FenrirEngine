# 02: Reload watcher as its own module

**What to build:** A maintainer can check when the file watcher decides a build is finished without sleeping or touching a file (spec: "Seam 3").

The decision "Idle, Settling or Ready" becomes its own module. Its inputs are the settings (hot reload, look interval, debounce), what the loaded generation's file looked like, the current time, what the file looks like now (a size and modification time, or nothing) and whether it can be opened. The generation lifecycle keeps reading the file and opening it, supplies those, and keeps loading, releasing and restoring generations. Its behaviour, including when `Poll` is Idle, does not change.

**Blocked by:** 01 (Rules test executable, change buffer cases and the probe).

**Status:** ready-for-agent

- [ ] The decision lives in a module with no file access, and the generation lifecycle delegates to it. Real reload behaviour is unchanged.
- [ ] Cases in the rules executable, driven by literal times and stamps, pin: a look sooner than the interval after the last is Idle; a stamp equal to the loaded one is Idle and clears what was pending; a file that cannot be looked at is Idle; the first sight of a new stamp is Settling; a stamp that changes again while settling restarts the wait; a stable stamp under the debounce is Settling; an unreadable one is Settling even after the debounce; a stable, readable stamp after the debounce is Ready; with hot reload off it is always Idle; marking a generation as loaded resets what was pending.
- [ ] The two end-to-end cases (the watcher reloads a real stable build between frames; hot reload off never acts but a manual reload works) are kept.
- [ ] The full CTest suite passes.
