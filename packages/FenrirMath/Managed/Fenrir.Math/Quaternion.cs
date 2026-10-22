namespace Fenrir.Math;

/// <summary>
/// A rotation, laid out exactly like FenrirMath's <c>Math::Quat</c> (<c>glm::quat</c>), which keeps <c>W</c> first in
/// memory. The fields are declared in that order on purpose: the host checks this struct against the native layout
/// when a script loads, and reordering them would fail the load.
/// </summary>
public struct Quaternion : IEquatable<Quaternion>
{
    public float W, X, Y, Z;

    public Quaternion(float x, float y, float z, float w)
    {
        X = x;
        Y = y;
        Z = z;
        W = w;
    }

    /// <summary>No rotation.</summary>
    public static Quaternion Identity => new(0, 0, 0, 1);

    // float.Equals, so equality agrees with the hash code (0 and -0 are equal and hash alike; NaN equals itself).
    public readonly bool Equals(Quaternion other) => X.Equals(other.X) && Y.Equals(other.Y) && Z.Equals(other.Z) && W.Equals(other.W);
    public override readonly bool Equals(object? obj) => obj is Quaternion other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public static bool operator ==(Quaternion left, Quaternion right) => left.Equals(right);
    public static bool operator !=(Quaternion left, Quaternion right) => !left.Equals(right);
    public override readonly string ToString() => $"({X}, {Y}, {Z}, {W})";
}
