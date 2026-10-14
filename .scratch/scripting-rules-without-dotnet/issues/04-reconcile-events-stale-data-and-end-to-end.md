# 04: Reconcile: events, stale data and the native check; shrink the lying assembly

**What to build:** The rest of reconcile's rules are checked through the same seam, and the hand-written lying assembly shrinks to what is needed end to end (spec: "Seam 2" and "Which existing cases move").

Events get the same rules as components, plus the one about a queue the app already owns. What the previous generation left behind is compared with what the new one defines. The check of native components against their C# structs sits where it does today, after the definitions' own rules and before anything is dropped or registered, and its failure can be supplied as data.

**Blocked by:** 03 (Reconcile over what the assembly reports: the seam and the component rules).

**Status:** resolved

- [x] Cases with the literal adapter pin the event refusals: reported twice; an impossible layout; a name the registry or an app queue already has, unless the previous generation owned the queue.
- [x] Cases pin the stale-data rules: a component whose layout is unchanged keeps its data; one that changed or no longer exists is removed from every scene with a warning that says how many were removed or that it no longer exists; an event queue whose layout changed is removed with its events, and one that is unchanged is kept with them.
- [x] A case pins that a failure of the native-against-C# check changes nothing (nothing dropped, nothing registered), and that it runs after the definitions' rules and before dropping. Dropping all script data on release empties what is owned.
- [x] The hand-written lying assembly keeps only what is needed for one end-to-end refusal per kind (component, event); the other modes and their switch are removed, and the end-to-end cases still pass.
- [x] `GLOSSARY.md` gains **Script report**: the components and events a script assembly says it defines, each with its size and alignment.
- [x] The full CTest suite passes.

## Comments

Done. The event rules, the stale-data rules (unchanged layout kept, changed or gone dropped with the warning, queues kept or made again), the native-check ordering and `DropScriptData` are cases in the rules executable (59 cases now); the end-to-end scripting suite is 72. The literal adapter counts how often the native check was asked, which pins that it is asked once, and only after the definitions' own rules pass. A mutation (the native check moved after dropping) fails a case.

Removed from the C#-driven suite: the impossible event layout and the event-named-like-an-app-queue cases, and with them the last layout mode of the hand-written assembly and its reflection helper. What it keeps end to end: the duplicate component and the duplicate event (real assembly, generated table, native function, refusal), the sound report, the failed-load rollback, the mismatched-struct cases and everything that really rebuilds.

The rules tests share their native test types in one header.
