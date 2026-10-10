# 15: Internal calls (managed → native)

**What to build:** A host application registers native functions that managed component code can call, which is the mechanism an engine needs to expose its API to scripts. Bindings target `delegate* unmanaged` static fields on component types, are validated when the component loads, and apply again for each newly loaded component.

**Blocked by:** 07

**Status:** ready-for-agent

- [ ] Native code registers a named function with its signature before or after host start-up. Registrations apply to components loaded afterwards.
- [ ] A component calling a bound function reaches native code with the correct arguments and return value (primitives, plus structs and strings once tickets 12 and 13 are complete).
- [ ] An unbound field, a signature mismatch or a missing registration produces a load-time or call-time diagnostic, never a crash on a null pointer.
- [ ] Native code that throws a C++ exception inside an internal call does not unwind through managed frames. The policy is documented and tested.
- [ ] Bindings do not keep a released component alive.
