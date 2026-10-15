# 11: Tagged-union components in C#

**What to build:** A native component holding a tagged union (a tag plus overlapping storage for one of several kinds) can be shared with C# and is checked like any other (spec: "FenrirScripting", "FenrirPhysics: scripting extension"). Iris's struct mapping already accepts explicit layout and matches fields by name, offset and kind, which should cover overlapping fields; a test proves it. If Iris rejects something, fix it additively (ADR-0006 allows "a call that used to fail and now succeeds"). The C# side is hand-written: an explicit-layout struct with a private tag, `[UnscopedRef]` checked views per kind that throw for the wrong kind, and implicit conversions from each kind.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [ ] A test native component with a tagged union (described with `IRIS_FIELD_AS`) validates against its C# mirror when the script assembly loads.
- [ ] A fixture C# system assigns a kind by value, edits a field through its view, and C++ reads the new kind and values (round trip both ways).
- [ ] Reading a view for the wrong kind throws `InvalidOperationException` in C#.
- [ ] A mirror with a wrong offset or a wrong field name is rejected at load with a diagnostic naming the field.
- [ ] FenrirScripting's README documents the pattern, including that script-defined components may use it by hand.

**Resolved by:** merge of impl/11-tagged-unions (f849008)
