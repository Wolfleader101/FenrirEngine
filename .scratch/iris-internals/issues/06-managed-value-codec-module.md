# 06: Managed value codec out of the registry

**What to build:** How a value crosses the managed bridge has one place to read and test (spec: "The managed registry split", value codec).

The conversions between the wire slot and a managed object, for every kind including struct, enum and array, move out of the registry into a module whose interface is a type and a value in and a value out. The managed checks stop keeping their own copies of the value-kind numbers and use the bridge's own definitions.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] The codec is its own module and the registry calls it. Behaviour of the bridge is unchanged.
- [x] Direct managed checks, with no component loaded, pin: a stray non-zero bool byte reads as true and is stored as one; an enum travels as its underlying integer and an out-of-range value is handled as today; a nested struct round-trips; strings, arrays and objects follow their documented slot rules.
- [x] Each of the checks that hard-coded value-kind numbers uses the bridge's own definitions instead.
- [x] Existing managed checks and the `Components` checks pass unchanged. The full CTest suite passes.

## Comments

Done. `ValueCodec` owns the slot-to-object and object-to-slot conversions: primitives, chars, strings (strict UTF-8 in, a length-prefixed block out), registered structs (with the stray-bool rule), nested structs and enums. A registered struct or enum is passed as a `StructRecord`, which moved to its own file; the registry looks it up and calls the codec, so the codec knows nothing about components. Objects and arrays are handles the registry owns, so the codec has no rule for them and asking for one is an argument error, which a check pins. `ValueKinds` keeps the kind rules.

The new group "value codec" runs with no component. A mutation (the bool fix-up only for bytes above 200) fails it with "A stray bool byte was not made one in the managed struct", while the existing structs group still passes, so the new check is not redundant. Every check file that hard-coded value-kind numbers now takes them from `ValueKind` (including the export-contract table and the array kinds in the unload checks). Behaviour of the bridge is unchanged.

Review follow-ups: one ordering difference, recorded: a struct slot of zero paired with a kind that names no registered struct used to be reported as "has no value" and is now reported as an unregistered kind (the registry looks the record up before calling the codec). Both are InvalidArgument for an argument that is wrong twice. The nested-struct round trip and its bool fix-up are separate checks, and the undefined-enum check compares the integer. The array bulk copy keeps its own loop that makes bool bytes 0 or 1; it moves with the array code if a later ticket splits that out.
