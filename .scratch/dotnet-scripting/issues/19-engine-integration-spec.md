# 19: Engine integration spec

**What to build:** A grilling session with the user that produces a separate spec and ticket set for integrating the stable hosting API into FenrirEngine: script discovery, script lifetime versus entity lifetime, the engine API surface exposed through internal calls, ECS bindings, editor reload, and threading. No implementation happens here.

**Blocked by:** 18

**Status:** ready-for-human

- [ ] An integration spec exists in its own feature directory, built only on the frozen public API.
- [ ] Open questions about threading, concurrency and hot-reload are either resolved or recorded as their own tickets.
- [ ] Any gaps in the hosting API that integration needs are filed as hosting tickets, not worked around inside the engine.
