# 01: Iris: string parameters for internal calls

**What to build:** an Iris user can register a C++ function that takes `std::string_view` parameters as an internal call (spec: `../spec.md`, "Iris: string parameters for internal calls"). This is an additive change under ADR-0006.

- **On the managed side**, Iris component code passes each string as a byte pointer followed by a 32-bit length. The bridge binds that pair to the string parameter.
- **Mismatched fields:** a field that declares anything else for a string parameter fails to load, and the diagnostic names the field.
- **What the function receives:**
  - the bytes exactly as sent (UTF-8, not checked), valid for the call;
  - an empty view when the length is zero.
- **Bad pairs:** a negative length, or a null pointer with a non-zero length, means the function is not called. An Error goes to the log sink and managed code gets a zero result, the same path as a caught C++ exception.

The bridge function table does not change, nor does any existing registration or behaviour.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] `RegisterInternalCall<&Function>` accepts functions with `std::string_view` parameters, mixed freely with today's kinds, and records them with the existing `String` value kind.
- [x] When binding a field, the bridge matches a registered string parameter against two consecutive field parameters: a pointer, then an `Int32`. A bare pointer, the wrong order, or a missing length fails the load, and the diagnostic names the field and shows both signatures.
- [x] ASCII, multi-byte UTF-8, embedded NUL and empty text all arrive byte for byte. A function mixing string and numeric parameters gets each in order.
- [x] A negative length, or a null pointer with a non-zero length, is refused: the function is not called, an Error reaches the log sink, and the result is value-initialized.
- [x] Registering the same string-taking function under the same name again succeeds.
- [x] Covered through Iris's public interface, in `components.cpp` and the managed `InternalCallChecks`, following their existing internal-call cases. The Iris example component's natives gain the fields the new cases need.
- [x] The `RegisterInternalCall` documentation, the Iris README's internal-call section and the glossary's **Internal call** entry say that strings may be parameters (not results).
- [x] All Iris checks pass (`IRIS_BUILD_TESTS`), and the engine's full CTest suite still passes.

## Comments

Done. The thunk Iris generates is now over wire parameters: a `std::string_view` crosses as a `const char*` and an `int32_t` length. `WireOf<T>` is the one place that knows how each kind crosses (its wire types, the validity check, the conversion), so another wire type later is one specialisation. A refused string is reported with its own log message ("given a string with a negative length, or with no bytes but a length"), through the same log sink path as a caught exception. Two existing checks expect 12 unbound fields instead of 10, because the example component gained `RecordText` and `StringBetweenNumbers`.
