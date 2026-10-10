# 07: Method handles and typed static invocation

**What to build:** Native code resolves a static method on a type handle by name plus parameter types (choosing between overloads) into a reusable method handle, then invokes it with typed blittable arguments and return values: fixed-width integers, floats, doubles, bool, and void. This replaces the `int(int,int)`-only invocation path.

**Blocked by:** 06

**Status:** resolved

- [x] The C++ API derives the managed parameter types from native types at compile time (for example a variadic `Invoke<R>(args...)`). Unsupported native types fail to compile.
- [x] Overloads resolve by exact parameter types. Ambiguous or missing overloads return an owning diagnostic.
- [x] Invocation validates argument count and types against the resolved method before calling it.
- [x] Managed exceptions return `ManagedException` (and `Overflow` where applicable) with diagnostics, and the next call succeeds.
- [x] Method handles are cached, generation-bound and stale-checked. The handle and its cache do not keep the component alive (verified with the unload check).
- [x] `InvokeInt32` is removed, and its tests migrate to the new API.

## Comments

From review 01 (D3 and C3): `InvokeComponentInt32` maps any `OverflowException` thrown by component code to `ManagedCallStatus::Overflow`, and `LoadComponent` maps `ArgumentException` to `InvalidArgument`. When typed invocation replaces `InvokeInt32`, component exceptions should all report `ManagedException`. Statuses should come only from the bridge's own validation.

**Done** (10/10 CTests in Debug and Release on Windows).
- `Type::FindMethod<R(Args...)>(name)` returns a `Method<R(Args...)>`; `Invoke(args...)` calls it. Parameter and return types come from the C++ types at compile time, and only `PrimitiveValue` types compile (a `static_assert` and a `requires` clause enforce it).
- Overloads resolve by exact parameter types through `GetMethod(name, types)`. The wire carries a `ValueKind` per value and one 64-bit slot per argument; the bridge checks the argument count on every call. A method that doesn't match is `NotFound`, with a diagnostic.
- `InvokeInt32`, `InvokeComponentInt32` and `ComponentInvocation` are removed (bridge table version 13). Every test, the example and the consumer use the new API.
- **Overflow:** the `Overflow` status (wire value 2) is removed. Every exception a method throws, including `OverflowException` and `ArgumentException`, is `ManagedException` with its own details and is not wrapped in `TargetInvocationException`. This follows the note recorded from review 01 (D3, C3).
- Method handles share the per-component handle table with type handles, so release invalidates them, ids are never reused, and they hold no managed reference after release.
- The scripted fixture bridge's control channel is now a method name (`Scenario:<name>`, `Select:<mode>`, `Counter:<name>`), driven through the public API.

Not done, by design: methods are static only. Instance methods arrive with objects in ticket 09.
