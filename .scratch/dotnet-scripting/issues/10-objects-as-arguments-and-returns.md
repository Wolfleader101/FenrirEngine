# 10: Objects as arguments and returns

**What to build:** Owned object handles can be passed as arguments to managed methods and constructors. Managed methods can return objects (including null), which arrive on the native side as new owned handles.

**Blocked by:** 09

**Status:** resolved

- [x] Passing an object handle as an argument works, and type mismatches are diagnosed before the call.
- [x] A returned object becomes a new, independent owned handle. Returning the same managed object twice produces handles whose release is correct either way, according to a documented policy.
- [x] A null return is represented explicitly and is not an error.
- [x] Passing a stale handle, or a handle from another component, is rejected.
- [x] Unload/ownership checks show that returned handles keep their component's unload blocked until they are released.

## Comments

**Done** (11/11 CTests in Debug and Release on Windows). Same bridge table (version 15); `ValueKind.Object` (12) added.
- Arguments: `const Object&` in a signature, for methods, instance methods and constructors. Returns: `std::optional<Object>`.
- **Type matching:** a native signature cannot name the managed type, so an `Object` kind matches any class or interface parameter or return type (not `string` or arrays), and the real type is checked when the call is made. A mismatch is `InvalidArgument`, before the method runs.
- **Ambiguity:** overloads that differ only by object parameter types make the lookup ambiguous (`InvalidArgument`, with a diagnostic). With the current API those overloads cannot be told apart from C++; naming the managed type is a possible later addition if a real requirement appears.
- **Return policy:** every return of an object gets a new, independent handle, even for the same managed object; releasing one never affects another. A null return is an empty optional. All handles are rooted in the component's table, so they block `Component::Release` until released.
- **Rejections:** a released or moved-from object is `InvalidHandle` and one from another component is `InvalidArgument`, both before the call natively; the bridge also checks every handle (unknown handle: `InvalidHandle`).
- **Overload matching is now manual** (a search over `GetMethods`/`GetConstructors` with exact kinds) rather than `GetMethod`'s binder, which also removes a source of the earlier widening bug.
- **Status mapping:** the bridge's own validation is `InvalidArgument`, a stale handle `InvalidHandle`, everything else `ManagedException`.
- The compile-fail check caught my change to the `static_assert` message, as designed.
