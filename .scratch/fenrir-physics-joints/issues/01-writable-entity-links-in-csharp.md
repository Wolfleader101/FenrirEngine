# 01: Writable entity links in C#

**What to build:** C# can make entity links, so every native component with an `Entity` field is writable from C# (spec: "Prerequisite: writable entity links (FenrirScripting)"). `EntityLink` gains an implicit conversion from `Entity`; one new core native function fills `owner` with the active scene's entity list, and `owner` is never readable from C#. `EntityLink.None` is the empty link (`value = 0xFFFFFFFF`, null owner); a zeroed `EntityLink` names entity 0 with no scene. C# names only entities of the active scene, including one made by `Entity.Create()` earlier in the same system (its value is reserved at once). The README's "Tagged unions" section says a `[TaggedUnion]` generator is planned, with its trigger: a script author wants a union, or a third engine union appears.

This is FenrirScripting work, independent of physics, and lands before any joints scripting.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] `ScriptingTests`: an `EntityLink` assigned from an `Entity` fills the owner; `EntityLink.None` is empty; a link to an entity created earlier in the same system reads back after playback.
- [x] `ScriptingTests`: a native component's `Entity` field written from C# reads correctly in C++.
- [x] `owner` is not readable from C#; a zeroed `EntityLink` names entity 0 with no scene and does not crash when read.
- [x] The README's "Tagged unions" section names the planned `[TaggedUnion]` generator and its trigger.

**Resolved by:** branch claude/joints-writable-entity-links-2f751c (not yet merged into dotnet-scripting). The proof is the `ScriptingTests` entity-link cases, with the fixture's `Links.cs` and a test-registered `Test.Tether` component. A zeroed link crashed C++'s `Entity::IsValid` (null entity list), so `IsValid` now returns false for an entity with no entity list.
