# 04: `FromTree`: the tolerant read, `@formerly`, unknown fields and types, `BridgeResult`

**What to build:** the second half of the bridge. A value tree fills a described value field by field and loads what it can: a bad field takes its default and is counted. This is how ADR-0077 §7's "a partly broken file loads the parts that read" is met (the spec, "Gaps settled here").

- **`FromTree(const Value&, const TypeDescription&, const TypeRegistry&, const Resolvers&, void* value, std::string_view path, ILogger&) -> BridgeResult`** fills a value its description's `Construct` made (the spec, "The bridge"). `path` names the tree's file in each diagnostic, as `ReadText`'s does (decided with the user on 2026-11-23).
  - **Missing field:** an absent field keeps its default.
  - **Wrong value:** each of these takes the field's default with one located diagnostic, counted in `defaulted`:
    - a value of the wrong kind;
    - an integer out of range for its kind;
    - a float in an integer field;
    - an unknown enumerator;
    - `null` in a field that does not admit it.
  - **Unknown key:** a key matching no field and no `@formerly` name is warned and dropped, counted in `unknownFields`.
  - **Former name:** a key matching a field's `@formerly` name reads into that field. When both names are present the current one wins and the former is dropped with a warning.
  - **Optional:** `null` in an `optional` reads as disengaged, and an absent one keeps its default (ticket 03 writes `null` for a disengaged one whose default holds a value).
  - **Fixed string:** text longer than N-1 bytes is cut by `ValueRef::SetText`. The bridge sees the shorter read-back and adds one diagnostic.
  - **Tagged union:** `kind` is read first and `SelectAlternative` is called before the live alternative fills. A union nested in a field, element or optional is reached only through a `ValueRef`, so FenrirTypes gains `ValueRef::SelectAlternative(tagValue)`, which forwards to the description's (decided with the user on 2026-11-23). A missing or unknown `kind` keeps the default alternative, with one diagnostic counted in `defaulted`.
- **Unknown types:** a canonical type name the registry lacks returns `BridgeStatus::UnknownType` and touches nothing. The caller keeps the subtree, and a test writes it back unchanged.
- **`BridgeResult`:** `status` (`Succeeded`, `Partial`, `UnknownType`), the counts `defaulted`, `unknownFields` and `unresolved`, and the `Diagnostics`, each logged once. The status is `Partial` whenever a count is non-zero.

**Blocked by:** 03.

**Touches:** `packages/FenrirSerialisation/` (`include/FenrirSerialisation/`, `src/`, `Tests/`, `Tests/CMakeLists.txt`), `packages/FenrirTypes/` (`include/FenrirTypes/ValueRef.hpp`, `src/ValueRef.cpp`, `Tests/GeneratedFixture.cpp`)

**Status:** resolved

- [x] For every kind, a struct goes to a tree and back and equals the original.
- [x] One test per tolerance rule asserts the count, the default taken and the diagnostic's line and column.
- [x] `@formerly` works both ways: the old name alone reads into the field, and with both names present the current one wins with a warning.
- [x] An unknown type returns `UnknownType` and leaves the value untouched, and its subtree writes back unchanged.
- [x] A tagged union read with `kind: "Sphere"` holds a sphere with the file's radius. An unknown `kind` holds the default alternative, with one diagnostic.
