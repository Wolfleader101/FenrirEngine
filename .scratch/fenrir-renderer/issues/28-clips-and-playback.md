# How are clips played?

Type: grilling
Status: claimed
Blocked by: 26

## Question

FenrirAnimation samples clips into a skeleton's base pose (ADR-0028). What does game code put on an entity to play one, and how does playback behave?

- The component (an `Animator`, a player, or other) and which entity carries it: the skeleton entity, or one that names it.
- Binding: clips name bones by name, resolved to indices once per clip and skeleton and cached; bones a clip lacks (rest pose), clip channels with no matching bone, and non-bone nodes a clip animates.
- Sampling: interpolation between keys (linear, step, cubic from glTF), quaternion handling, what ADR-0021's importer already normalises.
- Playback state: time, speed, looping, clamping, pausing; what time it advances by (frame delta in `PostUpdate`).
- Events in clips and gameplay synchronisation: how an event reaches game code (an ECS event, read once per reader as ADR-0012), events crossed by a loop or a large step.
- Root motion: in this spec or deferred.

## Comments

- From ticket 26 (ADR-0031): each clip channel keeps its target's source node identity and engine name; a clip binds to its own file's skeletons by identity and to others by engine name, with diagnostics for missing or ambiguous matches; channels on ordinary (non-bone) nodes are kept, and how they drive entities is this ticket's.
