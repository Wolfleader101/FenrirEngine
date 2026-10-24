using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Physics;

// The hand-written parts of FenrirPhysics's components (FenrirPhysics/Components.hpp). Every field, default and doc is in
// the generated mirror (Generated/, from the C++ header: ADR-0042); these parts add behaviour and what a C# caller needs
// beyond the C++ documentation. Each mirror is checked against its native layout whenever a script assembly loads, so a
// drift between the two fails the load and names the field. Edits take effect as they do in C++ (physics finds them by
// comparing, each tick). Each has the C++ defaults when made with `new`; `default` is all zero bytes, as in any struct.

/// <remarks>
/// From C#: writing a dynamic body's velocities sets them; writing its <see cref="Transform"/> teleports it.
/// </remarks>
public partial struct RigidBody
{
}

/// <remarks>
/// From C#: assign a kind to change it (<c>collider.Shape = new SphereShape { Radius = 1 }</c>) and edit one in place
/// through its view (<c>collider.Shape.Box.HalfExtents.Y = 2</c>).
/// </remarks>
public partial struct Collider
{
    public Collider(CollisionShape shape) => Shape = shape;
}
