namespace Fenrir.Scripting;

/// <summary>Three floats, laid out exactly like the engine's <c>glm::vec3</c>.</summary>
public struct Vector3 : IEquatable<Vector3>
{
    public float X, Y, Z;

    public Vector3(float x, float y, float z)
    {
        X = x;
        Y = y;
        Z = z;
    }

    public static Vector3 Zero => default;
    public static Vector3 One => new(1, 1, 1);

    public readonly float LengthSquared => X * X + Y * Y + Z * Z;
    public readonly float Length => MathF.Sqrt(LengthSquared);

    public static float Dot(Vector3 left, Vector3 right) => left.X * right.X + left.Y * right.Y + left.Z * right.Z;

    public static Vector3 operator +(Vector3 left, Vector3 right) => new(left.X + right.X, left.Y + right.Y, left.Z + right.Z);
    public static Vector3 operator -(Vector3 left, Vector3 right) => new(left.X - right.X, left.Y - right.Y, left.Z - right.Z);
    public static Vector3 operator -(Vector3 value) => new(-value.X, -value.Y, -value.Z);
    public static Vector3 operator *(Vector3 value, float scale) => new(value.X * scale, value.Y * scale, value.Z * scale);
    public static Vector3 operator *(float scale, Vector3 value) => value * scale;
    public static Vector3 operator /(Vector3 value, float divisor) => new(value.X / divisor, value.Y / divisor, value.Z / divisor);

    // float.Equals, so equality agrees with the hash code (0 and -0 are equal and hash alike; NaN equals itself).
    public readonly bool Equals(Vector3 other) => X.Equals(other.X) && Y.Equals(other.Y) && Z.Equals(other.Z);
    public override readonly bool Equals(object? obj) => obj is Vector3 other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y, Z);
    public static bool operator ==(Vector3 left, Vector3 right) => left.Equals(right);
    public static bool operator !=(Vector3 left, Vector3 right) => !left.Equals(right);
    public override readonly string ToString() => $"({X}, {Y}, {Z})";
}
