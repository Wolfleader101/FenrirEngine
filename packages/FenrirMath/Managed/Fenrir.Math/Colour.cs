namespace Fenrir.Math;

/// <summary>
/// A colour as linear red, green, blue and alpha, laid out exactly like FenrirMath's <c>Fenrir::Colour</c>. As in C++,
/// <c>new Colour()</c> is opaque white; <c>default</c> is all zeroes, transparent black. Colours written as sRGB hex
/// values are converted when made (<see cref="FromSrgb"/>).
/// </summary>
public struct Colour : IEquatable<Colour>
{
    public float R, G, B, A;

    /// <summary>Opaque white, as a default constructed <c>Fenrir::Colour</c> is.</summary>
    public Colour()
    {
        R = 1;
        G = 1;
        B = 1;
        A = 1;
    }

    public Colour(float r, float g, float b, float a = 1)
    {
        R = r;
        G = g;
        B = b;
        A = a;
    }

    /// <summary>
    /// The colour written as <c>0xRRGGBBAA</c> in sRGB: red, green and blue are decoded through the sRGB curve to
    /// linear, and alpha, which sRGB does not encode, is only scaled to [0, 1].
    /// </summary>
    public static Colour FromSrgb(uint rgba)
    {
        float Byte(int shift) => ((rgba >> shift) & 0xFFu) / 255.0f;
        return new(SrgbToLinear(Byte(24)), SrgbToLinear(Byte(16)), SrgbToLinear(Byte(8)), Byte(0));
    }

    // Opaque, in linear values, as C++'s named colours. Grey is sRGB mid grey (0x808080), which looks halfway between
    // black and white.
    public static Colour White => new(1, 1, 1);
    public static Colour Black => new(0, 0, 0);
    public static Colour Red => new(1, 0, 0);
    public static Colour Green => new(0, 1, 0);
    public static Colour Blue => new(0, 0, 1);
    public static Colour Yellow => new(1, 1, 0);
    public static Colour Cyan => new(0, 1, 1);
    public static Colour Magenta => new(1, 0, 1);
    public static Colour Grey => new(0.2158605f, 0.2158605f, 0.2158605f);

    // The sRGB transfer function's inverse (IEC 61966-2-1).
    private static float SrgbToLinear(float encoded) =>
        encoded <= 0.04045f ? encoded / 12.92f : MathF.Pow((encoded + 0.055f) / 1.055f, 2.4f);

    // float.Equals, so equality agrees with the hash code (0 and -0 are equal and hash alike; NaN equals itself).
    public readonly bool Equals(Colour other) =>
        R.Equals(other.R) && G.Equals(other.G) && B.Equals(other.B) && A.Equals(other.A);
    public override readonly bool Equals(object? obj) => obj is Colour other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(R, G, B, A);
    public static bool operator ==(Colour left, Colour right) => left.Equals(right);
    public static bool operator !=(Colour left, Colour right) => !left.Equals(right);
    public override readonly string ToString() => $"({R}, {G}, {B}, {A})";
}
