# 14: Arrays

**What to build:** Native code can create managed arrays, read and write their elements, and pass them to and from managed methods, for primitives, blittable structs and strings. An array handle has the same ownership semantics as an object.

**Blocked by:** 12, 13

**Status:** ready-for-agent

- [ ] Arrays can be created with a given element type and length, and their length can be read.
- [ ] Single-element and bulk copies in and out work for blittable elements. Out-of-range access returns a diagnostic.
- [ ] String arrays round-trip using the string contract from ticket 12.
- [ ] Arrays work as method arguments and return values, and a null array is distinct from an empty one.
- [ ] Array handles are owned, validated and stale-checked. Unload checks show they block component unload until released.
