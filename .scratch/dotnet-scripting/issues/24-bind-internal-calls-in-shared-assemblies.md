# 24: Bind internal calls in shared assemblies

**What to build:** Found while building engine ticket 02 (`.scratch/engine-scripting/`). The engine's API lives in the shared contract assembly `Fenrir.Scripting`, and the spec's binding pattern is a C# wrapper over a `delegate* unmanaged` field that the host registers with `RegisterInternalCall`. Iris bound those fields only in the loaded component assembly: the README said "fields in dependency assemblies are ignored", so a shared contract's native calls were never bound. Iris should bind a shared assembly's internal calls when it is shared, with the same rules as a component's.

**Blocked by:** none

**Status:** resolved

- [x] A shared assembly's public static function pointer fields are bound when it is shared, with the same rules and diagnostics as a component's.
- [x] A field that cannot bind fails the initialization with a diagnostic naming it, shares nothing, and the same sharing works once the function is registered.
- [x] A component that keeps a private copy of a contract it does not share is unaffected.
- [x] The README states the rule and the registration order.
- [x] Tests cover a bound shared call reached by two components, and the unbound-then-registered case.

## Comments

### Resolution

- **Managed:** `SharedAssemblies.Register` calls `InternalCalls.Bind` on the assembly it loads, before adding it to the shared set. Binding errors use the existing diagnostics, so a missing registration reads "no internal call is registered under this name".
- **Native:** `Host::Initialize` now attaches the registered internal calls before it shares assemblies (it was the other way round). That is the only behaviour change visible to a host: register internal calls before `Initialize`.
- **Compatibility (ADR-0006):** this is additive in practice. A shared assembly's function pointer fields were never bound, so any such field was a null pointer, and no working program could have depended on one. Initialization now fails loudly for a shared contract that declares a function pointer the host has not registered. If that is judged breaking, it needs its own ADR before release.
- **Tests:** the shared contract fixture gained `SharedNatives.Triple`, and `components.cpp` checks that two components that share the contract both reach the native function. The unbound runner checks that sharing fails with the field named when nothing is registered, and works once it is. ManagedChecks registers the function too.
- Bridge version unchanged: no wire or table change.
