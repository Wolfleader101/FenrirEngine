# How does physics step?

Type: grilling
Status: open
Blocked by: 04

## Question

How does FenrirPhysics run inside the app's frame?

- One physics step per `Tick`, or collision substeps inside it? Is the step rate `Time::tickRate`, or physics's own?
- Where do the sync systems run (ECS to physics before the step, physics to ECS after)? `SchedulePriority` has a single `Tick` phase: does FenrirScheduler need ordering inside `Tick` or new phases, and how is that kept usable by other packages?
- Rendering runs at frame rate between ticks: is there transform interpolation (a separate rendered/interpolated transform, or none), and who owns it?
- How do game systems that run in `Tick` see consistent state relative to the step (before or after)?
- How does stepping stay deterministic (see the map's Notes), given the threading answer from "What does Jolt require of a host engine?"?
