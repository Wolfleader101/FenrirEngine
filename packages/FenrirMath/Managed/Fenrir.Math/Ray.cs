namespace Fenrir.Math;

/// <summary>
/// A ray: an origin and a unit direction, laid out exactly like FenrirMath's <c>Math::Ray</c>. As in C++,
/// <c>new Ray()</c> starts at the origin and points along +Z, and a ray made from a direction normalizes it.
/// </summary>
public struct Ray : IEquatable<Ray>
{
    public Vector3 Origin;
    public Vector3 Direction;

    /// <summary>From the origin along +Z, as a default constructed <c>Math::Ray</c> is.</summary>
    public Ray()
    {
        Origin = Vector3.Zero;
        Direction = new Vector3(0, 0, 1);
    }

    /// <summary>From <paramref name="origin"/> along <paramref name="direction"/>, normalized.</summary>
    public Ray(Vector3 origin, Vector3 direction)
    {
        Origin = origin;
        Direction = direction / direction.Length;
    }

    public readonly bool Equals(Ray other) => Origin.Equals(other.Origin) && Direction.Equals(other.Direction);
    public override readonly bool Equals(object? obj) => obj is Ray other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(Origin, Direction);
    public static bool operator ==(Ray left, Ray right) => left.Equals(right);
    public static bool operator !=(Ray left, Ray right) => !left.Equals(right);
    public override readonly string ToString() => $"({Origin}, {Direction})";
}
