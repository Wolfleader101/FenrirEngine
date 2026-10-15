# 04: Scene resources

**What to build:** A package can keep its per-scene state in the scene, one value per type, ended with the scene (spec: "FenrirScene"; glossary: scene resource). A physics world will be one; navigation or audio could be others.

**Blocked by:** 01 (App runs one frame at a time with a chosen delta).

**Status:** ready-for-agent

- [ ] A scene offers typed `GetResource<T>` (null when absent), `AddResource<T>(args...)` (constructs, replacing any existing one) and `RemoveResource<T>`.
- [ ] A resource is destroyed when its scene is destroyed and when the App ends (test with a resource that records its destruction).
- [ ] Two scenes hold independent resources of the same type (test).
- [ ] The scene needs no knowledge of resource types (no engine package is a dependency of FenrirScene).
