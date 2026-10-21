# Spike: a lit mesh, a camera, a skybox and debug lines through the proposed seam

Type: prototype
Status: open
Blocked by: 08, 11, 12, 13, 14

## Question

Build a throwaway spike on a `prototype/fenrir-renderer-spike` branch that draws a lit mesh, a skybox and debug lines from a camera entity, through the proposed graphics seam, FenrirWindow and the ECS model, to check the decisions before the foundation spec is written.

- Does the backend stay hidden (no GL or windowing header reaches callers)?
- Does the seam's shape survive real GL code, and what would a second backend have to fight?
- Lessons for the spec, as the physics spikes recorded.
