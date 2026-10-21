# How do animated and simulated poses meet?

Type: grilling
Status: open
Blocked by: 27

## Question

Ragdoll glue writes simulated body poses into a skeleton as a modifier and reads the base pose to drive powered ragdolls (ADR-0028); the bone-to-body mapping and the bodies themselves are the physics map's. What does the animation side hand over?

- The first weighted override's representation for ragdolls: world-space targets resolved by finalisation, or local; per-bone weights, and setting a subtree's weight at once.
- Going limp and blending back: who ramps the weight, and what the glue writes each frame.
- Bones with no body: following their nearest mapped ancestor, or keeping the animated pose.
- Powered ragdolls: reading the base pose (local or model space) as motor targets, at which phase relative to physics ticks and interpolation.
- What the physics map's ragdoll spec receives: a note listing names, indices, hierarchy and the two poses, and that the bone-to-body mapping moved there.
- A headless test: a skeleton driven only by override targets, with no physics.
