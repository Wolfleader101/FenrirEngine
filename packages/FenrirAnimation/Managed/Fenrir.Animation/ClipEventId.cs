using System.Text;

namespace Fenrir.Animation;

/// <remarks>Make one from a name with <see cref="Of"/>, which C++'s <c>ClipEventId::Of</c> agrees with.</remarks>
public readonly partial struct ClipEventId : IEquatable<ClipEventId>
{
    private ClipEventId(uint value)
    {
        Value = value;
    }

    /// <summary>The id of <paramref name="name"/>: the 32-bit FNV-1a hash of its UTF-8 bytes, as C++ computes it.</summary>
    public static ClipEventId Of(ReadOnlySpan<char> name)
    {
        Span<byte> bytes = name.Length <= 256 ? stackalloc byte[Encoding.UTF8.GetMaxByteCount(name.Length)]
                                              : new byte[Encoding.UTF8.GetMaxByteCount(name.Length)];
        int length = Encoding.UTF8.GetBytes(name, bytes);
        uint hash = 2166136261u;
        foreach (byte b in bytes[..length])
        {
            hash ^= b;
            hash *= 16777619u;
        }
        return new ClipEventId(hash);
    }

    public bool Equals(ClipEventId other) => Value == other.Value;

    public override bool Equals(object? obj) => obj is ClipEventId other && Equals(other);

    public override int GetHashCode() => (int)Value;

    public static bool operator ==(ClipEventId a, ClipEventId b) => a.Equals(b);

    public static bool operator !=(ClipEventId a, ClipEventId b) => !a.Equals(b);

    public override string ToString() => $"ClipEventId({Value:x8})";
}
