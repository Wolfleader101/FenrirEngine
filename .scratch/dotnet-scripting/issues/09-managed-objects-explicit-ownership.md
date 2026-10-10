# 09: Managed objects with explicit ownership

**What to build:** Native code constructs a managed object from a type handle (choosing between constructor overloads with typed arguments) and receives an owning `ManagedObject` handle. It can call instance methods, including virtual ones, using the method-handle API, then release the object. The object stays alive across GC while owned. Release removes its root. Objects bound to a component block that component from unloading, according to a documented policy.

**Blocked by:** 07

**Status:** ready-for-agent

- [ ] Construction picks a constructor by its parameter types. A constructor that throws returns a diagnostic and leaves no root behind.
- [ ] Instance and virtual method calls work. Stateful calls show the same object being reused between calls.
- [ ] An owned object survives forced GC in the verification code. After release, its root is gone.
- [ ] Stale handles, double release and cross-component/cross-host handles behave as documented.
- [ ] Explicit unload is rejected while the component has live objects. After the final object is released, the component becomes collectible.
- [ ] Release is safe after the host wrapper has been destroyed. Destruction order and failed-construction paths are tested.
