# 15: Internal calls (managed → native)

**What to build:** A host application registers native functions that managed component code can call, which is the mechanism an engine needs to expose its API to scripts. Bindings target `delegate* unmanaged` static fields on component types, are validated when the component loads, and apply again for each newly loaded component.

**Blocked by:** 07

**Status:** resolved

- [x] Native code registers a named function with its signature before or after host start-up. Registrations apply to components loaded afterwards.
- [x] A component calling a bound function reaches native code with the correct arguments and return value (primitives, plus structs and strings once tickets 12 and 13 are complete).
- [x] An unbound field, a signature mismatch or a missing registration produces a load-time or call-time diagnostic, never a crash on a null pointer.
- [x] Native code that throws a C++ exception inside an internal call does not unwind through managed frames. The policy is documented and tested.
- [x] Bindings do not keep a released component alive.

## Comments

### Resolution

- Native: `Iris::RegisterInternalCall<&Function>("Namespace.Type.Field")` (`Iris/InternalCall.hpp`). The
  signature comes from the function; a generated thunk catches C++ exceptions (Error through the log
  sink, value-initialized result). Registrations live in leaked process-wide native state and are pushed
  to the bridge at `Host::Initialize` and on each later registration. Idempotent for the same function.
- Bridge: new `RegisterInternalCall` export (table slot 27, version 20); managed `InternalCalls` holds
  name -> (kinds, address) and `Bind` runs during `ComponentRegistry.Load`, before the component is
  registered, collecting every problem into one `InvalidArgument` and binding nothing on failure.
- Policy decision: **an unbound or mismatched field fails the component load** (rather than binding a stub),
  so a null function pointer can never be called. Consequence: every host that loads the example
  component must register its natives first (the example host, consumer and tests do).
- Strings and structs: unmanaged function pointers cannot take a managed string, and struct-by-value would
  need per-signature layout validation outside any component, so they cross as `(const char*, int length)` and a
  pointer. Struct-by-value internal calls are a candidate follow-up.
- Tests: `InternalCallChecks` (managed: unbound, mismatch, idempotency, conflict, bound calls), native cases
  in `components.cpp` (calls, exception policy via the log sink, registration rules, registration after
  start-up), and the `Components-unbound` CTest (a process where nothing is registered). Unload checks
  load the bound component and still collect it, showing bindings do not keep it alive.
