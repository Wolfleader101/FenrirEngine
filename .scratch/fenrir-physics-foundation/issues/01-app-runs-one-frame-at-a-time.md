# 01: App runs one frame at a time with a chosen delta

**What to build:** A test (or tool) can run the App one frame at a time as if a chosen number of seconds had passed, so that anything time-dependent (ticks, events, physics) can be tested deterministically (spec: "The test seam: running the App one frame at a time"). The first call runs the init phases. `App::Run` becomes a loop over the same frame with the wall-clock delta, behaving exactly as today. The scripting tests' headless app moves onto the new entry point.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [ ] The App has a public way to run one frame with a given delta; the first call runs `PreInit`/`Init`/`PostInit` once, later calls do not.
- [ ] A frame with delta `d` adds `d` to the accumulator and runs the fixed loop as many times as `d` (plus what was owed) covers, with no wall-clock reads.
- [ ] `App::Run` produces the same phase sequence as before, now built on the frame entry point.
- [ ] The scripting tests' headless app runs frames through the new entry point with a fixed delta, and the existing scripting tests pass.
- [ ] A test pins: a 1/60 s frame with a 1/60 s tick rate runs `Tick` once; 0.5/60 s runs it zero times; two of those run it once.
**Resolved by:** merge of impl/01-app-frame (d5e88b2)
