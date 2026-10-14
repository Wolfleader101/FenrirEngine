# 04: Reconcile: events, stale data and the native check; shrink the lying assembly

**What to build:** The rest of reconcile's rules are checked through the same seam, and the hand-written lying assembly shrinks to what is needed end to end (spec: "Seam 2" and "Which existing cases move").

Events get the same rules as components, plus the one about a queue the app already owns. What the previous generation left behind is compared with what the new one defines. The check of native components against their C# structs sits where it does today, after the definitions' own rules and before anything is dropped or registered, and its failure can be supplied as data.

**Blocked by:** 03 (Reconcile over what the assembly reports: the seam and the component rules).

**Status:** ready-for-agent

- [ ] Cases with the literal adapter pin the event refusals: reported twice; an impossible layout; a name the registry or an app queue already has, unless the previous generation owned the queue.
- [ ] Cases pin the stale-data rules: a component whose layout is unchanged keeps its data; one that changed or no longer exists is removed from every scene with a warning that says how many were removed or that it no longer exists; an event queue whose layout changed is removed with its events, and one that is unchanged is kept with them.
- [ ] A case pins that a failure of the native-against-C# check changes nothing (nothing dropped, nothing registered), and that it runs after the definitions' rules and before dropping. Dropping all script data on release empties what is owned.
- [ ] The hand-written lying assembly keeps only what is needed for one end-to-end refusal per kind (component, event); the other modes and their switch are removed, and the end-to-end cases still pass.
- [ ] `GLOSSARY.md` gains **Script report**: the components and events a script assembly says it defines, each with its size and alignment.
- [ ] The full CTest suite passes.
