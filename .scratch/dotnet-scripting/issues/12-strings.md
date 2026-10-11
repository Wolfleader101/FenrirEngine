# 12: Strings

**What to build:** UTF-8 strings work as method arguments, return values and field/property values. Native input is borrowed for the duration of the call. Results are owning `std::string`s. Null strings are distinct from empty ones.

**Blocked by:** 07

**Status:** resolved

- [x] String arguments decode strictly. Malformed UTF-8 is rejected with `InvalidArgument` before managed code runs.
- [x] String returns arrive as owning UTF-8 and preserve embedded nulls and non-ASCII text. There is no fixed size limit and no size-then-call-again round trip that could run managed code twice.
- [x] A null managed string is distinguishable from an empty one on the native side.
- [x] String fields and properties work once ticket 11 is complete (a follow-up note is acceptable if 11 is not yet done).
- [x] Any UTF-8 coverage that ticket 03 kept fixture-only is now covered here. (The fixture scenarios that remain test diagnostic/name text transport, not string values; see Resolution.)

## Comments

From the ticket 03 review: removing `GetGreeting` and `EchoUtf8` lost the only real-bridge checks of *returned* non-ASCII and embedded-null UTF-8 payloads. Only the fixture covers them now. String returns here must restore that coverage against the real bridge: multi-byte scalars, empty, and embedded null.

### Resolution

- Wire: kind 13 (`String`). An argument slot points at a borrowed 16-byte `StringArgument` (data, length,
  null flag) on the native stack; decoding is strict (`UTF8Encoding(throwOnInvalidBytes)`) in
  `ConvertArguments`, so a malformed argument fails before the method runs. A returned string is a
  bridge-allocated block (8-byte length + UTF-8 bytes; zero is null) that native copies once and gives back
  with the new `FreeString` export (table slot 20, bridge version 17). No size query, no cap.
- Native: `std::string_view` / `std::optional<std::string_view>` arguments, `std::optional<std::string>`
  returns and member values (`Member<std::optional<std::string>>::Set` takes `std::optional<std::string_view>`).
- The "fixture-only" UTF-8 path: the remaining fixture scenarios (`EmbeddedNull`, `InvalidUtf8`,
  `CopyEmbeddedNull`, `CopyInvalidUtf8`) test the *diagnostic and name* text transport, which has no real
  counterpart that can produce malformed bytes, so they stay. Returned non-ASCII, empty and embedded-null
  values now run against the real bridge (`StringChecks`, components.cpp string cases).
- A returned string with an unpaired surrogate is `InvalidArgument` (it has no UTF-8 form).
- Known limits: `CreateObject` deduces its argument types, so pass `std::string_view` explicitly rather than a
  `const char*` or `std::string`; a string longer than 2 GiB is rejected as `InvalidArgument`.
- `NativeMemory`-owned blocks leak only if native never frees; the native side frees the block when copying or
  completing the call throws (`bad_alloc`), and `FreeString(0)` is ignored.
