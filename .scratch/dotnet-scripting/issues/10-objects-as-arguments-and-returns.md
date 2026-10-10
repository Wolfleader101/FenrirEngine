# 10: Objects as arguments and returns

**What to build:** Owned object handles can be passed as arguments to managed methods and constructors. Managed methods can return objects (including null), which arrive on the native side as new owned handles.

**Blocked by:** 09

**Status:** ready-for-agent

- [ ] Passing an object handle as an argument works, and type mismatches are diagnosed before the call.
- [ ] A returned object becomes a new, independent owned handle. Returning the same managed object twice produces handles whose release is correct either way, according to a documented policy.
- [ ] A null return is represented explicitly and is not an error.
- [ ] Passing a stale handle, or a handle from another component, is rejected.
- [ ] Unload/ownership checks show that returned handles keep their component's unload blocked until they are released.
