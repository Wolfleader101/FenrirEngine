namespace Fenrir.Physics;

// The hand-written parts of FenrirPhysics's collision shapes (FenrirPhysics/Components.hpp); the fields, defaults and
// docs are in the generated mirrors (Generated/), and the scripting SDK's tagged-union generator writes CollisionShape's
// constructor, views and conversions from its generated storage.

/// <remarks>From C#, a mesh is made by <see cref="PhysicsWorld.CreateCollisionMesh"/>.</remarks>
public partial struct CollisionMesh : IEquatable<CollisionMesh>
{
    public readonly bool IsNone => Id == 0;

    public readonly bool Equals(CollisionMesh other) => Id == other.Id;
    public override readonly bool Equals(object? obj) => obj is CollisionMesh other && Equals(other);
    public override readonly int GetHashCode() => Id.GetHashCode();
    public static bool operator ==(CollisionMesh left, CollisionMesh right) => left.Equals(right);
    public static bool operator !=(CollisionMesh left, CollisionMesh right) => !left.Equals(right);
}

/// <remarks>
/// From C#: laid out as the native <c>Fenrir::CollisionShape</c> (a one-byte tag, then the kinds overlapping at the
/// union's offset, 8 because a <see cref="CollisionMesh"/> holds a 64-bit id). Assigning a kind changes it; each kind's
/// view reads or edits it in place and throws <see cref="InvalidOperationException"/> if the shape is another kind. New,
/// it is a unit box.
/// </remarks>
public partial struct CollisionShape
{
}
