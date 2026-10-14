# How do C# components hold a tagged union?

Type: grilling
Status: open
Blocked by: none

## Question

`Collider` holds a `CollisionShape`, a plain-data tagged union ("How is physics modelled in the ECS?"). C# can express one as a blittable struct with overlapping fields (`[StructLayout(LayoutKind.Explicit)]` with `[FieldOffset]`), but FenrirScripting's component mapping and source generator do not support it today.

- How a native component containing a tagged union is described to scripting (its registration), and how the mapping checks the C# layout against the C++ one.
- What the generated C# looks like: the tag, a checked typed view per kind (`Shape.Box.HalfExtents`), and implicit conversions (`Shape = new BoxShape(...)`).
- Whether a C# component defined in a script assembly may declare its own tagged union, or only native components may.
- Whether this is a general scripting capability (other likely users: light kinds, camera projections), and so belongs in a scripting ADR.
