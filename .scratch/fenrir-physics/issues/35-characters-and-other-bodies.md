# How does a character interact with other bodies?

Type: grilling
Status: open
Blocked by: 30, 32

## Question

How do characters and the rest of the physics world affect each other? Builds on [What does Jolt offer for characters?](30-what-jolt-offers-for-characters.md) and [How is a character body modelled in the ECS?](32-character-bodies-in-the-ecs.md).

- **Pushing:** a character pushing dynamic bodies, and how hard (Jolt's max strength, or a neutral setting).
- **Being pushed:** by dynamic bodies, by kinematic bodies (doors, paddles), by joints.
- **Standing on bodies:** dynamic bodies under a character (does it press down on them?), kinematic platforms (riding them).
- **Character against character:** colliding, pushing each other, cost.
- **Filtering:** a character's `CollisionFilter` layers and mask, and whether general collision groups (in the map's fog) are now needed.
- **Queries:** whether raycasts, shape casts and overlaps hit characters, and what they name.

Facts from "What does Jolt offer for characters?": pushing is an immediate impulse clamped by max strength (100 N default) and never downward; the character is pushed only during its own update. Platform velocity comes through `GetGroundVelocity`. Character against character is an app-supplied interface (brute force by default, O(N²), not thread safe). Without an inner body, queries do not see a character; with inner bodies, each character's queries skip only its own.

From "How do other engines expose characters?": pushing ranges from game code in a hit callback (Unity, Avian) to automatic impulses (Rapier, which can launch light bodies at 44× the character's speed) and mass-scaled forces (Unreal); only a dynamic-body character (Tnua) is pushed back. Raycasts see characters everywhere; Rapier's kinematic character misses fixed sensors by default; O3DE's two bodies per character is a filed bug.
