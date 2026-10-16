# 02: Fixed-step phases and a cap on ticks per frame

**What to build:** Packages can hook before and after the simulation without ordering tricks, and a slow frame no longer spirals (spec: "FenrirApp, FenrirScheduler, FenrirTime"). A frame runs `PreUpdate`, `BeforeTicks` once, then `PreTick`, `Tick`, `PostTick` per fixed step, then `AfterTicks` once (even with no tick), `Update`, `PostUpdate`. `Time` gains `maxTicksPerFrame` (default 5): time owed beyond it is dropped from the accumulator with a warning.

**Blocked by:** 01 (App runs one frame at a time with a chosen delta).

**Status:** resolved

- [ ] `SchedulePriority` has `BeforeTicks`, `PreTick`, `PostTick`, `AfterTicks`, run in the order above.
- [ ] Tests (through the frame seam): a frame with zero ticks still runs `BeforeTicks` and `AfterTicks` once and no `PreTick`/`Tick`/`PostTick`; a frame with three ticks runs `PreTick`→`Tick`→`PostTick` three times in that interleaving.
- [ ] A frame owing more than `maxTicksPerFrame` ticks runs exactly that many, empties the accumulator and logs one warning naming the dropped time.
- [ ] The DemoApp and scripting keep working unchanged.

**Resolved by:** merge of impl/02-phases (ca45ad5)

**Hand checked (2026-10-16):** the DemoApp and its scripting demo behave as before with the new phases.
