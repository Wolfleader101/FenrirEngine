# 08: A script reload rematerialises from kept trees

**What to build:** a C# data asset survives a script reload that changes its shape, because the store's tree is the truth (ADR-0083, ADR-0077). See the spec's "Reload".

- **The V2 fixture.** FenrirAssets' scripting tests gain a `FENRIR_TEST_FIXTURE_V2` build of their fixture, as FenrirScripting's tests have. In it, the C# data asset type drops a field, adds one, moves to version 2 and gains an upgrader from 1.
- **On reload:**
  - the old type and its upgraders are unregistered, and the new ones registered;
  - the kept tree rematerialises on the next `TryGet`, with the dropped field warned once and the added field defaulted;
  - a file written at version 1 is upgraded when the store re-reads it.

**Blocked by:** 07.

**Touches:** `packages/FenrirAssets/Scripting/Tests/` (the V2 fixture, its CMake and the reload tests), `packages/FenrirScripting/src/` if `Forget` needs a fix that this ticket finds

**Status:** ready-for-agent

- [ ] After a reload into V2, `TryGet` gives the asset with the new shape, its kept values and the added field's default, and warns once for the dropped field.
- [ ] A version-1 file is upgraded through the C# upgrader when it is read again.
- [ ] No generation-cache entry or codec from V1 survives the reload.
