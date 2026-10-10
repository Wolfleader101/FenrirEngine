# 09: Managed objects with explicit ownership

**What to build:** Native code constructs a managed object from a type handle (choosing between constructor overloads with typed arguments) and receives an owning `ManagedObject` handle. It can call instance methods, including virtual ones, using the method-handle API, then release the object. The object stays alive across GC while owned. Release removes its root. Objects bound to a component block that component from unloading, according to a documented policy.

**Blocked by:** 07

**Status:** resolved

- [x] Construction picks a constructor by its parameter types. A constructor that throws returns a diagnostic and leaves no root behind.
- [x] Instance and virtual method calls work. Stateful calls show the same object being reused between calls.
- [x] An owned object survives forced GC in the verification code. After release, its root is gone.
- [x] Stale handles, double release and cross-component/cross-host handles behave as documented.
- [x] Explicit unload is rejected while the component has live objects. After the final object is released, the component becomes collectible.
- [x] Release is safe after the host wrapper has been destroyed. Destruction order and failed-construction paths are tested.

## Comments

**Done** (11/11 CTests in Debug and Release on Windows). Bridge table version 15; new `InUse` status (7).
- `Type::CreateObject(args...)` picks a constructor by exact parameter types (no widening) and returns an owning, move-only `Object`. `Type::FindInstanceMethod<Signature>` finds instance methods (possibly inherited); `InstanceMethod::Invoke(object, args...)` calls them, and a virtual method dispatches to the override of the object's real type.
- **Rooting:** the only strong reference to an owned object is an entry in the component's handle table. `ObjectChecks` holds a `WeakReference`: the object survives forced GC while owned, and is collected after release.
- **Release policy (decided here):**
  - `Component::Release()` is refused with `InUse` while objects are live, and the component stays loaded and usable.
  - Destroying a `Component` always force-releases (`ForceReleaseComponent`), so a leaked object can never pin a registration; objects it still held then return `InvalidHandle`.
  - `Object::Release()` is idempotent; it returns `InvalidHandle` if the component was already released first.
  - Handles are validated per component: an object from another component, or an unknown or released handle, is rejected (`InvalidHandle` from the bridge, `InvalidArgument` natively for another component's method). An object of the wrong type for a method is `InvalidArgument`.
- **Unload:** `ManagedUnload` creates an object in every cycle, checks that a release with a live object is refused, releases the object, and then sees the component collected. A second scenario leaks an object, force-releases, and still sees collection.
- **Failure paths:** a constructor exception leaves nothing rooted (checked by the live-object count and by a following successful `Component::Release`); an abstract type, a static type and a widening-only match are `NotFound`.

Not done, by design: objects cannot be passed to or returned from methods yet (ticket 10), and fields and properties arrive with ticket 11.
