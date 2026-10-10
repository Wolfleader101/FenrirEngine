# 18: API stabilisation pass

**What to build:** A second full review and cleanup over the finished API, followed by a freeze of the public headers as the stable surface that engine integration (and other C++ projects) will build on. Includes the docs, a glossary, and an honest record of which platforms have been tested.

**Blocked by:** 05, 08, 17

**Status:** ready-for-agent

- [ ] The review is written up like ticket 01's. Its findings are fixed or explicitly deferred.
- [ ] Naming, result shapes, ownership semantics and error statuses are consistent across every public type.
- [ ] Public headers contain no SDK types, OS handles or managed implementation details.
- [ ] The README/usage guide covers every feature with short examples, plus deployment requirements (runtime, bridge, native libraries, component dependencies).
- [ ] The glossary defines the core terms (host, bridge, component, handle, release vs collection, generation, internal call).
- [ ] The consumer project from ticket 05 is updated to the final API and builds and runs.
- [ ] A platform matrix states what has been verified on Windows, Linux and macOS, and notes CI as future work.
