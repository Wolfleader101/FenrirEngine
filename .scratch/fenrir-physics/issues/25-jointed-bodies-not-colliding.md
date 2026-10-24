# How do jointed bodies stop colliding?

Type: grilling
Status: resolved
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

From "How are joints modelled in the ECS?" (ADR-0013): a body may take part in any number of joints (each its own joint entity), so the mechanism must handle a body excluded from several neighbours; the flag is expected as a shared field on `Joint`.

## Answer

No ADR: the default is cheap to flip before release and the mapping is private. Glossary: **Joint** gains that its bodies do not collide with each other unless it says so; **collision layer**'s note on collision groups stays.

- **Default and field:** a shared field on `Joint`, `bool collideConnected = false;`. Jointed bodies do not collide unless the joint opts in (Godot, O3DE; Avian and Rapier default the other way). A door against its frame, chain links and later ragdoll neighbours usually overlap at the rest pose. Box2D's name, read positively (`true` means they collide); `disableCollision` rejected as a double negative in C#. A live edit, applied on the next tick through the diff.
- **Only removes:** a joint can only remove contact, never add it. A pair collides only when the layer rule allows it ("How do collision layers and filtering work?") and no enabled joint between them excludes it; `collideConnected = true` never overrides the filter.
- **Direct pairs only:** only the two bodies a joint names. In a chain A–B–C, A and C still collide (every engine surveyed). Excluding a whole chain is a general collision group, left to ragdolls and character bodies. When several joints join one pair, any one enabled joint with `collideConnected = false` excludes it, independent of order.
- **Compounds and triggers:** every solid collider of both bodies is excluded (a compound is one body; the filter belongs to the body). Triggers are not, even a trigger child of a jointed body: they are their own backend bodies with their own filter, which game code uses to stop them reporting the other body.
- **Lifetime:** the exclusion holds only while the joint is enabled and active (both names valid bodies, "What happens at a joint's edges?"); an inactive joint excludes nothing, and physics drops its pair from the table wherever 26 removes its constraint. A disabled or broken joint ("What joint state can game code read, and how do joints break?") lets its bodies collide from the next step, so a snapped link hits its neighbour; re-enabling restores it. This avoids Godot's exception outliving its joint (#115015). A joint to the world (`bodyB` empty) excludes nothing.
- **Queries:** unaffected. Queries ignore joints entirely; `QueryFilter`'s ignored body covers a body querying past itself.
- **Contact events:** an exclusion starting between touching bodies (`collideConnected` set false, a joint created or re-enabled) ends their contact with the usual `ContactEnd` on the next step; one ending sends `ContactBegin` when they touch again. Nothing special to joints.
- **Private mapping** (inside `src/Backend/Jolt/`): one custom Jolt `GroupFilter` over a pair → count table of excluding enabled joints.
  - Only bodies in at least one excluding joint get a `CollisionGroup`, its group ID the body's index; other bodies skip the filter.
  - The table is patched on the main thread in `PostTick` with the joint diff, before the step, so it is read-only while job threads call the filter.
  - Rejected: `GroupFilterTable` (cannot express a body jointed to two neighbours) and rejecting contacts in `OnContactValidate` (after narrow-phase work, and tangled with the contact-event listener).
  - Room for general collision groups: each body keeps one group pointing at this filter, so they become one more test inside it, with no public change.
- **Sleep:** settled by 26: every diffed change to a `Joint` wakes both its bodies, so a `collideConnected` toggle or an enable or disable takes effect on a sleeping pair at once.
- **Left to other tickets:** the C# field (28); demo content (29).
