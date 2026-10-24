namespace Fenrir.Math;

/// <summary>Two floats, laid out exactly like FenrirMath's <c>Math::Vec2</c> (<c>glm::vec2</c>).</summary>
public struct Vector2 : IEquatable<Vector2>
{
    public float X, Y;

    public Vector2(float x, float y)
    {
        X = x;
        Y = y;
    }

    public static Vector2 Zero => default;
    public static Vector2 One => new(1, 1);

    public readonly float LengthSquared => X * X + Y * Y;
    public readonly float Length => MathF.Sqrt(LengthSquared);

    public static float Dot(Vector2 left, Vector2 right) => left.X * right.X + left.Y * right.Y;

    public static Vector2 operator +(Vector2 left, Vector2 right) => new(left.X + right.X, left.Y + right.Y);
    public static Vector2 operator -(Vector2 left, Vector2 right) => new(left.X - right.X, left.Y - right.Y);
    public static Vector2 operator -(Vector2 value) => new(-value.X, -value.Y);
    public static Vector2 operator *(Vector2 value, float scale) => new(value.X * scale, value.Y * scale);
    public static Vector2 operator *(float scale, Vector2 value) => value * scale;
    public static Vector2 operator /(Vector2 value, float divisor) => new(value.X / divisor, value.Y / divisor);

    // float.Equals, so equality agrees with the hash code (0 and -0 are equal and hash alike; NaN equals itself).
    public readonly bool Equals(Vector2 other) => X.Equals(other.X) && Y.Equals(other.Y);
    public override readonly bool Equals(object? obj) => obj is Vector2 other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y);
    public static bool operator ==(Vector2 left, Vector2 right) => left.Equals(right);
    public static bool operator !=(Vector2 left, Vector2 right) => !left.Equals(right);
    public override readonly string ToString() => $"({X}, {Y})";
}
