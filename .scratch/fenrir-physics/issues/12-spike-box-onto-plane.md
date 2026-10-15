# Spike: a box dropping onto a plane through the proposed seam

Type: prototype
Status: open
Blocked by: 03, 04, 05, 13

## Question

Do the decided seam, ECS model and stepping actually work together? Build a throwaway spike in the DemoApp: a static plane and a dynamic box, made through FenrirPhysics's proposed public interface with Jolt behind it, stepped in `Tick` and drawn through the existing `Transform` rendering. React to it: does the public interface read well, does the sync feel right, does anything about Jolt leak through? Keep it on a `prototype/fenrir-physics-spike` branch and link it here.

Also confirm the build settings from "How does Jolt join the build?": FenrirPhysics with Jolt builds in every preset (Windows Clang and MSVC, debug and release; Linux) with those overrides, and note any line `libs/CMakeLists.txt` needed.

