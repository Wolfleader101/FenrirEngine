# 14: Arrays

**What to build:** Native code can create managed arrays, read and write their elements, and pass them to and from managed methods, for primitives, blittable structs and strings. An array handle has the same ownership semantics as an object.

**Blocked by:** 12, 13

**Status:** resolved

- [x] Arrays can be created with a given element type and length, and their length can be read.
- [x] Single-element and bulk copies in and out work for blittable elements. Out-of-range access returns a diagnostic.
- [x] String arrays round-trip using the string contract from ticket 12.
- [x] Arrays work as method arguments and return values, and a null array is distinct from an empty one.
- [x] Array handles are owned, validated and stale-checked. Unload checks show they block component unload until released.

## Comments

### Resolution

- Native: `Array<T>` (`Iris/Array.hpp`) for primitive, mapped-struct and `std::string` elements; `Create`, `Length`,
  `Get`, `Set`, `CopyIn`, `CopyOut`, `Release`, `AsObject`. Passed as `const Array<T>&` / `const Array<T>*`,
  returned as `std::optional<Array<T>>`.
- Wire: an array is an ordinary rooted object handle (same table as objects, so ownership, `InUse` and forced
  release come for free). Kind 15 | (element kind << 8), or 16 | (struct handle << 8). Five new exports
  (`CreateArray`, `GetArrayLength`, `GetArrayElement`, `SetArrayElement`, `CopyArray`); table slot 26, bridge version 19.
  Each element call carries the expected array kind and the bridge checks the real element type.
- Bulk copy uses `MemoryMarshal.GetArrayDataReference` with the verified element size (bool 1 byte, struct =
  `Marshal.SizeOf`); string arrays are element-by-element only.
- Unload: `UnloadChecks` now creates an array, shows release is refused while it lives, and shows the component
  unloads and is collected after it is freed (and that a forced release drops a leaked one).
- Not done (candidates for later): arrays as field/property values; arrays of objects, jagged or multidimensional
  arrays; `Array<T>` from a managed method on an instance method is supported but untested separately.
