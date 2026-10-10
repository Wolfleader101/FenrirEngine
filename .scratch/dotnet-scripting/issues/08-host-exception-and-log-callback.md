# 08: Host-wide exception and log callback

**What to build:** A host user can register a native sink that receives managed exception reports and bridge log messages (with severity), so an embedding application can route them into its own logger. Per-call diagnostics on results stay as they are.

**Blocked by:** 02

**Status:** resolved

- [x] The host exposes a way to register and clear a sink. Its lifetime and threading expectations are documented.
- [x] A managed exception raised during a bridge call reaches the sink and is also still returned in the call's own diagnostic.
- [x] Bridge log messages reach the sink with a severity level. Without a sink, nothing breaks.
- [x] A sink that throws, or is cleared mid-session, cannot corrupt the host or let an exception cross the boundary.
- [x] A test registers a sink, triggers both message kinds, and checks the content without asserting localized wording.

## Comments

**Done** (11/11 CTests in Debug and Release on Windows). Bridge table version 14.
- `Host::SetLogSink(LogSink)` registers one process-wide sink (an empty sink clears it) through a new `SetLogSink` bridge export. There is one bridge per process, so there is one sink. It survives the host that set it, and clearing is possible from any initialized host.
- **Messages:** every exception a bridge export catches is reported with its full `ToString()`: `Error` for component code, `Warning` for the bridge's own validation (`BridgeArgumentException`, malformed UTF-8). Bridge events (component loaded or released, assembly shared) are `Info`. A `NotFound` is not an exception and is not logged. The call still returns its own diagnostic.
- **Safety:** the native trampoline swallows exceptions from the sink; the sink state is never destroyed, so a late callback from managed code cannot touch freed memory; a sink that clears or replaces itself keeps running because the trampoline holds its own reference; managed code never calls the sink while holding a registry lock.
- **Not done, by design:** messages sent during `Initialize`, before a sink can be set, are not delivered. `LogLevel::Debug` exists but nothing emits it yet.
- Tests: managed `LogChecks` through the real export with a real `UnmanagedCallersOnly` callback, and four native cases (severity, no sink and clearing, throwing and self-clearing sinks, uninitialized host and host destruction). Content is checked without asserting localized wording.
