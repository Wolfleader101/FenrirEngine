# Where does the backend seam go?

Type: grilling
Status: resolved
Blocked by: 01, 02

## Question

What is the shape of the seam between FenrirPhysics and its physics backend?

- Is the backend chosen at run time (an interface FenrirPhysics calls through) or at build time (one backend compiled in, behind private headers)?
- What vocabulary does FenrirPhysics own (body, shape, collider, material, layer, body handle), and how does it map onto Jolt's without depending on it?
- How are backend IDs and lifetimes kept out of the public interface (opaque handles, PIMPL)?
- What happens to the leaks every surveyed engine hit ("How do other engines hide their physics backend?"): backend-specific tuning settings, behaviour differences between backends, an escape hatch to native backend objects. Refused, contained, or accepted?
- Is there a test-only fake backend, so FenrirPhysics can be tested without Jolt, or are its tests run against Jolt?

Builds on "What does Jolt require of a host engine?" (01) and "How do other engines hide their physics backend?" (02). Likely an ADR: it is hard to reverse, and it records the compile-level hiding that goes beyond ADR-0008.

## Answer

Recorded as ADR-0010, `docs/adr/0010-fenrirphysics-hides-its-backend-at-compile-time.md`; the concept names are in `GLOSSARY.md` ("FenrirEngine physics").

- **Build-time seam in one place:** a private concrete backend class (no virtual calls); its only implementation is `src/Backend/Jolt/`, the only code that includes Jolt. Jolt is linked `PRIVATE`; no public header names it.
- **Public identity is entities and components.** No public body handle; FenrirPhysics maps entities to backend ids privately.
- **Leaks:** backend tuning settings and native escape hatches refused; behaviour differences accepted (promised only where tests pin it); threading contained (public calls from the App's thread, never during a step; the backend's job system on FenrirScheduler's `ThreadPool`).
- **Tests** go through the public interface against real Jolt; no fake backend.
- **Entry point:** `Physics::Install(App&, PhysicsConfig)` returns the owner of the backend's process-wide state, its systems and settings.
- **Vocabulary:** rigid body (motion type static/kinematic/dynamic), soft body, character body, collider, collision shape, physics material, trigger, collision layer (with a collision mask).
