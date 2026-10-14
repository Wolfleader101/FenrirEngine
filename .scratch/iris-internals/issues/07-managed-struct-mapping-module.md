# 07: Managed struct mapping out of the registry

**What to build:** The description format for mapped structs and the code that checks it change as one module (spec: "The managed registry split", struct mapping).

Description parsing and the checks of the description against the managed type (blittable rules, sizes, offsets, field order, nested types) move out of the registry, together with the record they produce.

**Blocked by:** 06 (Managed value codec out of the registry).

**Status:** ready-for-agent

- [ ] The struct mapping is its own module and the registry calls it. Behaviour is unchanged.
- [ ] Direct managed checks, with no component loaded, pin each mismatch message: a wrong size, a wrong offset, a wrong field name or order, a wrong field kind, a nested type not registered first, a non-blittable type, an inline array of the wrong length or element.
- [ ] Registering the same type again still validates again and returns the same handle.
- [ ] Existing managed checks and the `Components` checks pass unchanged. The full CTest suite passes.
