# 04: A missing contract is a logged error

Spec: [Building a game against the engine](../spec.md).

**What to build:** When the scripting host cannot start .NET because a contract assembly it needs is missing beside the executable, it logs an error naming that contract, instead of running on silently without scripts.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirScripting/src/`, `packages/FenrirScripting/Tests/`

**Status:** ready-for-agent

Runs beside the stack, off the base: its files are disjoint from 01, 02, 03 and 05.

- [ ] A FenrirScripting test starts the host with one contract assembly removed and checks that an error naming it is logged.
- [ ] With every contract present, nothing new is logged and the scripting tests pass as before.
