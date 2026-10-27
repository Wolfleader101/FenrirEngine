---
status: accepted
---

# Natives pass enums, text and wrappers by value, and bindings use other bindings' mirrors

Porting the hand-written extensions onto ADR-0042's generated natives showed four things the natives format and the generated binding could not express, each of which a hand-written facade was papering over:

- **Text.** Iris passes a `std::string_view` parameter on the wire as a pointer and a length, and the natives of assets, window and debug draw take one. describe-api refuses it, classifying it as a struct, so the generated binding could only declare text written as `const char*` and `std::int32_t`, which the built-in natives do and nothing else does.
- **Enums.** Iris passes no enum by value, so every native took a key, button or cursor mode as its underlying integer and the facade cast it. The generated declaration would read `KeyIsDown(ushort key)` where the C++ API reads `IsDown(Key)`.
- **Id structs.** `WindowId`, `GamepadId` and `CollisionMesh` each wrap one integer. Iris passes no struct by value, so the natives took the integer and the facade unwrapped it. The SDK's native-function generator already special-cased one such wrapper, `Entity`, which crosses as its `Value`.
- **Other packages' types.** A binding that reads another package's description mirrors every type in it. FenrirWindow's events hold FenrirInput's `WindowId`; the renderer's components hold input's `WindowId` and assets' handles; every extension holds math's types. Each would generate a second mirror of a type another contract already has, unless the binding lists it by hand as a bound type.

We decided:

- **Text is a kind of the natives format.** describe-api records a `std::string_view` parameter as kind `string`; Iris.Bindings declares it as `ReadOnlySpan<char>` over the wire pair (a pointer, an `int` length) that Iris already uses. A `string` argument converts implicitly, so callers written against a `string` declaration do not change. The natives keep `std::string_view`.
- **Enums cross by value as their underlying integer.** Iris accepts an enum parameter and result, wired as its underlying integer: the native thunk casts, and the managed check sees the integer kind it already knows. Iris.Bindings declares the described C# enum; the SDK's generator casts to the underlying type for the function pointer field.
- **One-field structs cross as their field.** Iris gains a wrapper trait (`Iris::WrapperOf<T>` naming the field's type) under which a struct is wired as that field and the thunk builds the struct, so a native takes `WindowId` as the C++ API does, with no alias and no cast. The SDK's generator gets the same rule for C#: an unmanaged struct with exactly one field, or one auto-property, of a pass-through type crosses as that member. It replaces the `Entity` special case rather than adding to it. Iris.Bindings maps a described one-field struct to the bound or mirrored type the binding names for it.
- **A binding uses other bindings' mirrors.** A binding configuration lists the bindings whose mirrors it uses (`[uses]`); a type one of them mirrors is named by that binding's C# type and mapping header rather than generated again. Window uses Input's; Renderer uses Input's and Assets'; Physics and the five use Math's. A used binding's bound types are inherited the same way (each entry naming its mapping's header), so a vector every binding holds is named once, by the binding that owns it.
- **`char32_t` is a `u32`.** The description records it as an unsigned 32-bit integer, as it records `char` as a byte, so `TextEvent`'s codepoint is described from its header and the stand-in that existed for it goes.

What stays as it was: a struct of more than one field crosses by pointer (`in T`, `out T`, or a span), `AssetHandle<T>` crosses as the one 64-bit number its two fields pack into, with the facade packing, and a bound type is still mapped by hand.

## Considered options

- **Teach describe-api nothing and rewrite each text native as `const char*` plus a length.** Rejected: every native that takes text would be rewritten for the format, and Iris already takes the view.
- **Keep enums and ids as integers with casts in the facades.** Works today and costs nothing, but the generated declarations would read as integers where the C++ reads as types, and the facades would keep a cast per native.
- **Full struct-by-value in Iris**, a new wire kind carrying the struct's size, which the platform ABI makes sound on both sides. Rejected for now: nothing passes a struct by value. Every vector crosses by pointer, and the five extensions' by-value structs are all one-field wrappers. It stays open for the day a native wants a `Vec3` by value.
- **Name every other package's type as a bound type in each configuration.** Works, which is what `Vector3` does today, but grows to dozens of hand-kept lines across the bindings that a `[uses]` line replaces.
- **A `char32` kind.** Rejected: C# has no 32-bit char, and Iris maps fields by kind and offset, so the integer kind is exactly what the mirror holds.

## Consequences

- ADR-0042's "native functions are not in the description" consequence stands; its statement that a binding reads descriptions and binds every type in them is amended by `[uses]`.
- Iris's native boundary and its managed bridge change (enums, the wrapper trait) in one ticket with the SDK's generator; describe-api and Iris.Bindings change in a second, since the generators emit what the runtime must accept.
- The natives description's `SCHEMA.md` and `natives-description.md` gain the `string` kind; `api-description.md` the `u32` note for `char32_t`.
- `GLOSSARY.md` gains *wrapper*.
- C#'s `Entity` has one C++ spelling across the bindings, FenrirScripting's `EntityValue` (a wrapper of the entity's 32-bit value, with its mapping and wrapper trait in `EntityValueMapping.hpp`): each binding whose natives take, return or hold one describes it as a bound type of its own selection and names it `Entity`. The physics and debug draw copies of that struct, and the renderer's and physics's `EntityId` aliases mapped through `[natives] types`, go, so a native returns an `Entity` as it takes one and no facade re-wraps a result.
- A pointer a native's doc marks `@param[in]` is one value whatever parameter follows it: the natives description records it as `input`, and Iris.Bindings takes it as `in T` rather than a span when a count follows. The rule that a pointer followed by a count is a span stays the default; the mark is for the native whose next parameter happens to be a count (`DebugDraw.Grid`'s cells).
