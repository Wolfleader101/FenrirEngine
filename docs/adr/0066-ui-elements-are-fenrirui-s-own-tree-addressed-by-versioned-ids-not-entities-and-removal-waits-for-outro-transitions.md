---
status: proposed
---

# UI elements are FenrirUI's own tree addressed by versioned ids, not entities, and removal waits for outro transitions

The engine is ECS-first (ADR-0007), and Bevy shows a UI can be made of entities with components. The question for FenrirUI was whether an element is an entity. Settled in a grilling session on 2026-11-10.

## Elements are not entities

An entity belongs to a scene; a UI root belongs to a window, an image or a world quad, and outlives scene changes (a pause menu over any scene). A UI tree is thousands of nodes with ordered children, a style cascade that walks ancestors and a layout that walks everything, which is the access pattern an ECS serves worst, and C# reaches elements through handles anyway (ADR-0061). So **FenrirUI keeps elements in a dense tree of its own** and hands out an **`ElementId`**: an index and a version, like `Entity`'s shape but not one, so a removed and reused slot is detected as stale. Elements are found by id string, by class query and by walking children. A game that wants an element tied to an entity (a name plate over a character) keeps the pair in a component of its own. **The one entity in the design is the world-space quad**: an entity with a component naming the root it shows, its size in world units and its UI units per world unit (ADR-0060's world-space target), and the game gives that root a pointer ray each frame; the engine never decides which camera a world UI is seen from (ADR-0057's line).

## Removal is deferred through `:outro`

Enter and exit animations are what the web makes hard and s&box made easy. An element carries the `:intro` pseudo-class from creation until its first layout completes, and `Remove()` adds `:outro` and removes the element only when its transitions have finished (immediate removal is a flag). A stylesheet animates both with ordinary transitions. The cost, accepted: a removed element lingers for its transition's length, still laid out and still hit-testable unless the stylesheet turns its pointer events off, and code holding its id sees it alive until then.

## Considered

- **Elements as entities with components** (Bevy). Free C# access, queries and events, at the cost of every element carrying scene ownership, the cascade and layout running as queries over archetypes, and children ordered by a component the ECS has no notion of.
- **Removal at once, animation by the game.** Every exit animation would be a game-side timer and a second removal call; s&box's users write `Delete()` and a transition.
