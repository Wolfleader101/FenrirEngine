namespace Fenrir.Math;

/// <summary>An axis-aligned box from its smallest corner to its largest, laid out exactly like FenrirMath's
/// <c>Math::AABB</c>.</summary>
public struct Bounds : IEquatable<Bounds>
{
    public Vector3 Min;
    public Vector3 Max;

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
