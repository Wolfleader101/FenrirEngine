# How do jointed bodies stop colliding?

Type: grilling
Status: open
Blocked by: 20, 22

## Question

Two bodies joined by a joint often must not collide (a door and its frame, chain links). Only this per-joint behaviour is in the joints spec; general collision groups wait for ragdolls and character bodies. Builds on [What does Jolt offer for constraints?](20-what-jolt-offers-for-constraints.md) and [How are joints modelled in the ECS?](22-joints-in-the-ecs.md).

- **The default:** connected bodies collide or not by default, and the field that switches it.
- **How it combines with `CollisionFilter`:** whether the joint can only remove contacts the layer rule allows, never add them.
- **Compound bodies:** whether every collider of each body is excluded, and triggers (a trigger child of a jointed body).
- **Chains:** only direct neighbours, or every body in a connected chain.
- **Private mapping:** onto Jolt's group filter (or a contact-listener rejection), kept inside the backend directory; whether the mapping leaves room for the later general collision groups.
- **Queries:** whether the setting affects queries at all (presumably not).

Facts from "What does Jolt offer for constraints?": Jolt gives each body only one `CollisionGroup`, applied after the layer test (it can only remove pairs) and not to queries. A body jointed to two different neighbours cannot be expressed with `GroupFilterTable` alone, so a custom group filter (run on job threads) or another mechanism may be needed.
