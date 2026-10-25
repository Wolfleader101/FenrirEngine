namespace Fenrir.Math;

// The behaviour of the generated Bounds (Generated/Bounds.cs, from FenrirMath's Math::AABB): an axis-aligned box from its
// smallest corner to its largest.
public partial struct Bounds : IEquatable<Bounds>
{
    public Bounds(Vector3 min, Vector3 max)
    {
        Min = min;
        Max = max;
    }

    public readonly bool Equals(Bounds other) => Min.Equals(other.Min) && Max.Equals(other.Max);
    public override readonly bool Equals(object? obj) => obj is Bounds other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(Min, Max);
    public static bool operator ==(Bounds left, Bounds right) => left.Equals(right);
    public static bool operator !=(Bounds left, Bounds right) => !left.Equals(right);
    public override readonly string ToString() => $"({Min}, {Max})";
}
