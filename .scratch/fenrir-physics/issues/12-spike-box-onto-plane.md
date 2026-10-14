# Spike: a box dropping onto a plane through the proposed seam

Type: prototype
Status: open
Blocked by: 03, 04, 05

## Question

Do the decided seam, ECS model and stepping actually work together? Build a throwaway spike in the DemoApp: a static plane and a dynamic box, made through FenrirPhysics's proposed public interface with Jolt behind it, stepped in `Tick` and drawn through the existing `Transform` rendering. React to it: does the public interface read well, does the sync feel right, does anything about Jolt leak through? Keep it on a `prototype/fenrir-physics-spike` branch and link it here.
