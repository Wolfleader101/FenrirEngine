# Spike: a character walking a small level through the proposed model

Type: prototype
Status: open
Blocked by: 32, 33

## Question

Do the character model from [How is a character body modelled in the ECS?](32-character-bodies-in-the-ecs.md) and the per-tick move from [How does a character move each tick?](33-how-a-character-moves-each-tick.md) work in practice? Build a throwaway spike in the DemoApp on a `prototype/fenrir-physics-character-spike` branch, in its own worktree: a character driven from the keyboard through a small level with a walkable slope and a too-steep one, a flight of stairs, a moving kinematic platform and a few pushable boxes, made through the proposed public interface with Jolt behind the seam. Check it by hand in the DemoApp, as the earlier spikes were.

React to it: does the interface read well; does the ground state hold steady on slopes and stairs; does riding the platform feel right; does pushing boxes look right; does anything about Jolt leak. Record the lessons the spec needs.

The spike's scene may later become the C++ character demo scene (see "What do the character demo scenes show?").
