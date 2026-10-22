namespace Fenrir.Math;

/// <summary>Four floats, laid out exactly like FenrirMath's <c>Math::Vec4</c> (<c>glm::vec4</c>).</summary>
public struct Vector4 : IEquatable<Vector4>
{
    public float X, Y, Z, W;

    public Vector4(float x, float y, float z, float w)
    {
        X = x;
        Y = y;
        Z = z;
        W = w;
    }

    public static Vector4 Zero => default;
    public static Vector4 One => new(1, 1, 1, 1);

    public readonly float LengthSquared => X * X + Y * Y + Z * Z + W * W;
    public readonly float Length => MathF.Sqrt(LengthSquared);

    public static float Dot(Vector4 left, Vector4 right) =>
        left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W;

    public static Vector4 operator +(Vector4 left, Vector4 right) =>
        new(left.X + right.X, left.Y + right.Y, left.Z + right.Z, left.W + right.W);
    public static Vector4 operator -(Vector4 left, Vector4 right) =>
        new(left.X - right.X, left.Y - right.Y, left.Z - right.Z, left.W - right.W);
    public static Vector4 operator -(Vector4 value) => new(-value.X, -value.Y, -value.Z, -value.W);
    public static Vector4 operator *(Vector4 value, float scale) =>
        new(value.X * scale, value.Y * scale, value.Z * scale, value.W * scale);
    public static Vector4 operator *(float scale, Vector4 value) => value * scale;
    public static Vector4 operator /(Vector4 value, float divisor) =>
        new(value.X / divisor, value.Y / divisor, value.Z / divisor, value.W / divisor);

    // float.Equals, so equality agrees with the hash code (0 and -0 are equal and hash alike; NaN equals itself).
    public readonly bool Equals(Vector4 other) =>
        X.Equals(other.X) && Y.Equals(other.Y) && Z.Equals(other.Z) && W.Equals(other.W);
    public override readonly bool Equals(object? obj) => obj is Vector4 other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public static bool operator ==(Vector4 left, Vector4 right) => left.Equals(right);
    public static bool operator !=(Vector4 left, Vector4 right) => !left.Equals(right);
    public override readonly string ToString() => $"({X}, {Y}, {Z}, {W})";
}
