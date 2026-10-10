# 08: Host-wide exception and log callback

**What to build:** A host user can register a native sink that receives managed exception reports and bridge log messages (with severity), so an embedding application can route them into its own logger. Per-call diagnostics on results stay as they are.

**Blocked by:** 02

**Status:** ready-for-agent

- [ ] The host exposes a way to register and clear a sink. Its lifetime and threading expectations are documented.
- [ ] A managed exception raised during a bridge call reaches the sink and is also still returned in the call's own diagnostic.
- [ ] Bridge log messages reach the sink with a severity level. Without a sink, nothing breaks.
- [ ] A sink that throws, or is cleared mid-session, cannot corrupt the host or let an exception cross the boundary.
- [ ] A test registers a sink, triggers both message kinds, and checks the content without asserting localized wording.
