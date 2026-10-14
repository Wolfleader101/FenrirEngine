# 06: Managed value codec out of the registry

**What to build:** How a value crosses the managed bridge has one place to read and test (spec: "The managed registry split", value codec).

The conversions between the wire slot and a managed object, for every kind including struct, enum and array, move out of the registry into a module whose interface is a type and a value in and a value out. The managed checks stop keeping their own copies of the value-kind numbers and use the bridge's own definitions.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] The codec is its own module and the registry calls it. Behaviour of the bridge is unchanged.
- [ ] Direct managed checks, with no component loaded, pin: a stray non-zero bool byte reads as true and is stored as one; an enum travels as its underlying integer and an out-of-range value is handled as today; a nested struct round-trips; strings, arrays and objects follow their documented slot rules.
- [ ] Each of the checks that hard-coded value-kind numbers uses the bridge's own definitions instead.
- [ ] Existing managed checks and the `Components` checks pass unchanged. The full CTest suite passes.
