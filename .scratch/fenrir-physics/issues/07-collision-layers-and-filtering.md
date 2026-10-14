# How do collision layers and filtering work?

Type: grilling
Status: open
Blocked by: 01, 03

## Question

What does game code see for deciding what collides with what, and how does it map onto the backend?

- A named-layer collision matrix (Unity-style), category/mask bits (Box2D-style), or both?
- How many layers, declared how (at app setup, data-driven), and can they change at run time?
- How does FenrirPhysics derive Jolt's object layers and broad-phase layers from it, without exposing them?
- Per-body ignore rules (e.g. a character ignoring its own ragdoll) — in the foundation or left to the feature families?
