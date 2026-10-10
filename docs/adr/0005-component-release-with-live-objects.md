---
status: accepted
---

# Refuse to release a component that has live objects, but always release on destruction

`Component::Release()` returns `InUse` and leaves the component loaded while any `Object` created from it is alive. Destroying a `Component`, by contrast, always force-releases it: the bridge drops the objects it still held, and those `Object`s then return `InvalidHandle`.

We chose this over two alternatives:

- **Always drop the objects on release.** An explicit `Release()` would silently invalidate objects native code still owns, and the first sign would be a later `InvalidHandle`. Making the refusal explicit puts the mistake at the call that caused it.
- **Refuse in the destructor too.** A destructor cannot report failure, so a leaked `Object` would pin the registration, and its collectible context, for the life of the process. That breaks the guarantee that destroying an owner always requests unloading.

## Consequences

- Reload and shutdown code must release objects before releasing a component; destroying the `Component` first is allowed but invalidates them.
- An `Object` can be destroyed in any order relative to its `Component` and `Host`; releasing one whose component is gone returns `InvalidHandle` (and its destructor ignores that).
- The bridge keeps the policy in one place (`ComponentRegistry.TryRelease`), with a `ForceReleaseComponent` export for the destructor path.
