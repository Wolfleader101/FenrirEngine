# Spike: a character that falls as a ragdoll and gets up, through the proposed model

Type: prototype
Status: claimed
Blocked by: 51, 52, 53, 54

## Question

Do the collision groups from [How do general collision groups work?](51-general-collision-groups.md), the model from [How is a ragdoll modelled in the ECS, and where does the glue live?](52-ragdolls-in-the-ecs.md), the description and builder from [From what description is a ragdoll built, and with which bodies, joints, motors and strengths?](53-ragdoll-description-bodies-joints-and-motors.md) and the modes from [How does a ragdoll switch between following and simulating, and how is it powered?](54-ragdoll-switching-powering-and-following.md) work in practice? Build a throwaway spike on a `prototype/fenrir-physics-ragdolls-spike` branch, in its own worktree: a skinned walking character (the AnimationDemo's Quaternius human or CesiumMan, tick-posed) whose ragdoll is built from a description, following its bones as kinematic hit boxes; a key drops it into simulating (with a push), another makes one arm limp, another powers it towards the walk, and another gets it up with a blend back; boxes to fall onto and a second ragdoll to collide with; a character body under the skeleton that the ragdoll ignores. Made through the proposed public interface with Jolt behind the seam, in the demo scaffold (a PhysicsDemo scene, or the AnimationDemo if physics is easier to add there). Check it by hand, as the earlier spikes were.

React to it: does the interface read well; does a derived ragdoll stand and fall believably with default limits and masses; do the hit boxes follow the walk with no lag and the right velocity; does the switch pop or fly; does a limp arm hang from its kinematic shoulder without stretching; does a powered ragdoll hold a walk pose and recover from a shove; does the skeleton entity follow the hips and does getting up blend back without a jump; do the two ragdolls collide with each other but not with themselves or their characters; is it deterministic (two runs bit-identical); does anything about Jolt leak. Record the lessons the spec needs.

The spike's scene may later become the C++ ragdoll demo scene (see "What do the ragdoll demo scenes show?").
