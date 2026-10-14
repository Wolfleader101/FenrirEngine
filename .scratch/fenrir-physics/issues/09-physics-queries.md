# What does the query API look like?

Type: grilling
Status: open
Blocked by: 03, 07

## Question

How does game code ask the physics world questions?

- Which queries in the foundation: raycast (closest, all), shape cast, overlap (shape, point)?
- What a hit returns (entity, point, normal, distance, collider) and how the result is returned without allocating per call.
- Filtering: by layer (from "How do collision layers and filtering work?"), by excluded entities, by sensors in or out.
- Which state a query sees: the last completed step, and whether it reflects ECS changes made since.
- How a system reaches the API (through the physics world, a free function taking the app/scene).
