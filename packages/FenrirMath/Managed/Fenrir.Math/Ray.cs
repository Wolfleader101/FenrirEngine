namespace Fenrir.Math;

// The behaviour of the generated Ray (Generated/Ray.cs, from FenrirMath's Math::Ray): an origin and a unit direction. As
// in C++, new Ray() starts at the origin and points along +Z, and a ray made from a direction normalizes it.
public partial struct Ray : IEquatable<Ray>
{
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
