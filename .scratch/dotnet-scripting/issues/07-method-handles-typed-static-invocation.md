# 07: Method handles and typed static invocation

**What to build:** Native code resolves a static method on a type handle by name plus parameter types (choosing between overloads) into a reusable method handle, then invokes it with typed blittable arguments and return values: fixed-width integers, floats, doubles, bool, and void. This replaces the `int(int,int)`-only invocation path.

**Blocked by:** 06

**Status:** ready-for-agent

- [ ] The C++ API derives the managed parameter types from native types at compile time (for example a variadic `Invoke<R>(args...)`). Unsupported native types fail to compile.
- [ ] Overloads resolve by exact parameter types. Ambiguous or missing overloads return an owning diagnostic.
- [ ] Invocation validates argument count and types against the resolved method before calling it.
- [ ] Managed exceptions return `ManagedException` (and `Overflow` where applicable) with diagnostics, and the next call succeeds.
- [ ] Method handles are cached, generation-bound and stale-checked. The handle and its cache do not keep the component alive (verified with the unload check).
- [ ] `InvokeInt32` is removed, and its tests migrate to the new API.

## Comments

From review 01 (D3 and C3): `InvokeComponentInt32` maps any `OverflowException` thrown by component code to `ManagedCallStatus::Overflow`, and `LoadComponent` maps `ArgumentException` to `InvalidArgument`. When typed invocation replaces `InvokeInt32`, component exceptions should all report `ManagedException`. Statuses should come only from the bridge's own validation.
