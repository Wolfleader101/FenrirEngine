namespace Fenrir.Math;

/// <summary>
/// A 4x4 matrix, laid out exactly like FenrirMath's <c>Math::Mat4</c> (<c>glm::mat4</c>): four columns, so a
/// transform's translation is <see cref="Column3"/>. The fields are declared in column order on purpose: the host
/// checks this struct against the native layout, and reordering them would fail the check.
/// </summary>
public struct Matrix4x4 : IEquatable<Matrix4x4>
{
    public Vector4 Column0, Column1, Column2, Column3;

    public Matrix4x4(Vector4 column0, Vector4 column1, Vector4 column2, Vector4 column3)
    {
        Column0 = column0;
        Column1 = column1;
        Column2 = column2;
        Column3 = column3;
    }

    /// <summary>The identity: ones on the diagonal, zeroes elsewhere.</summary>
    public static Matrix4x4 Identity => new(new(1, 0, 0, 0), new(0, 1, 0, 0), new(0, 0, 1, 0), new(0, 0, 0, 1));

    public readonly bool Equals(Matrix4x4 other) =>
        Column0.Equals(other.Column0) && Column1.Equals(other.Column1) && Column2.Equals(other.Column2) &&
        Column3.Equals(other.Column3);
    public override readonly bool Equals(object? obj) => obj is Matrix4x4 other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(Column0, Column1, Column2, Column3);
    public static bool operator ==(Matrix4x4 left, Matrix4x4 right) => left.Equals(right);
    public static bool operator !=(Matrix4x4 left, Matrix4x4 right) => !left.Equals(right);
    public override readonly string ToString() => $"({Column0}, {Column1}, {Column2}, {Column3})";
}
