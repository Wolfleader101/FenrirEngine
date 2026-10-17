# Spike: a hinged door and a hanging chain through the proposed model

Type: prototype
Status: open
Blocked by: 22, 23

## Question

Does the joint model from [How are joints modelled in the ECS?](22-joints-in-the-ecs.md) and the kinds from [Which joint kinds are there, and what does each expose?](23-joint-kinds-and-settings.md) work in practice? Build a throwaway spike in the DemoApp on a `prototype/fenrir-physics-joints-spike` branch: a door on a hinge (with a limit and a motor) hung from a static frame, and a chain of boxes hanging from a point fixed to the world, made through the proposed public interface with Jolt behind the seam. Check it by hand in the DemoApp, as "Spike: a box dropping onto a plane through the proposed seam" was.

React to it: does the interface read well; are anchor frames intuitive under parented and scaled bodies; what does editing a setting cost (rebuilds); does anything about Jolt leak; do jointed bodies stay stable. Record the lessons the spec needs.

The spike's scene may later become one of the joints demo scenes (see "What do the joints demo scenes show, and how do several scenes switch?").

From "What happens at a joint's edges?" (26): try the risky edges by hand. Destroy a chain link's entity mid-swing (the joints touching it go inactive with one warning each; the rest of the chain wakes and falls; no crash in Release, since constraints are removed before bodies). Teleport the door alone (it is pulled back to its frame) and then the door and frame together (no jolt). Scale and reshape a link (the anchors stay put). Check that a live motor edit wakes a sleeping door, and that solve order set from joint entity order through Jolt's private constraint priority actually changes nothing visible after removals. Record how violent a single-body teleport is.
