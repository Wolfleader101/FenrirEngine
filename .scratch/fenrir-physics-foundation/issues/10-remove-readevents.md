# 10: Remove App::ReadEvents (contract)

**What to build:** The old way of reading events, which shows each event in two frames, is gone, so nothing can read events twice by accident (ADR-0012). Contract step after the migrations.

**Blocked by:** 08 (Event readers in C#, and scripting migrates); 09 (DemoApp readers migrate).

**Status:** ready-for-agent

- [ ] `App::ReadEvents<T>()` and the shared combined read buffer are deleted; the type-erased raw read used by scripting goes through readers too.
- [ ] Every build preset and every test passes with no remaining caller.
- [ ] The glossary's "Event" entry and any docs that described the two-frame window are current.
