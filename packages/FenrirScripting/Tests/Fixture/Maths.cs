using System.Runtime.Loader;
using Fenrir.Math;

namespace Fenrir.ScriptFixture;

// The Fenrir.Math contract seen from a script: what the tests compare with FenrirMath's C++ types.
public static class Maths
{
    // One element of a matrix, by column and then row, as glm indexes it.
    public static float MatrixElement(Matrix4x4 matrix, int column, int row)
    {
        Vector4 picked = column switch
        {
            0 => matrix.Column0,
            1 => matrix.Column1,
            2 => matrix.Column2,
            _ => matrix.Column3,
        };
        return row switch
        {
            0 => picked.X,
            1 => picked.Y,
            2 => picked.Z,
            _ => picked.W,
        };
    }

    public static Matrix4x4 Identity() => Matrix4x4.Identity;

    public static Colour NewColour() => new();

    public static Colour FromSrgb(uint rgba) => Colour.FromSrgb(rgba);

    public static Colour Grey() => Colour.Grey;

    public static Ray NewRay() => new();

    public static Ray RayAlong(float x, float y, float z) => new(Vector3.Zero, new Vector3(x, y, z));

    // Fenrir.Math came from the host's shared copy, in the default load context.
    public static bool MathIsShared() =>
        AssemblyLoadContext.GetLoadContext(typeof(Vector3).Assembly) == AssemblyLoadContext.Default;

    public static string MathLocation() => typeof(Vector3).Assembly.Location;
}
