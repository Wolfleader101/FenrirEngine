# 03: Stable scenes, and DestroyScene keeps the active scene

**What to build:** Creating a scene never invalidates entities in other scenes, and destroying a scene never silently changes which scene is active (spec: "FenrirApp, FenrirScheduler, FenrirTime"). Today scenes are stored by value in a growing container, so `CreateScene` can move every scene and leave each `Entity`'s pointer to its entity list dangling; and `DestroyScene` erases by index without adjusting the active index.

**Blocked by:** 01 (App runs one frame at a time with a chosen delta).

**Status:** resolved

- [ ] Scenes live at stable addresses: an `Entity` from a scene, and a reference to the scene, stay valid after many `CreateScene` calls (test).
- [ ] Destroying a scene before the active one keeps the same scene active (test).
- [ ] Destroying the active scene is either refused with a logged error or has one defined, documented result, and a test pins it.
- [ ] `GetActiveScene`, `ChangeActiveScene`, `GetScene` and `ForEachScene` behave as before otherwise.

**Resolved by:** merge of impl/03-stable-scenes (3f012ae)
