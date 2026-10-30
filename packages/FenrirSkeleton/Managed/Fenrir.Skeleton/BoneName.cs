using System.Text;
using Fenrir.Scripting;

namespace Fenrir.Skeleton;

/// <remarks>
/// Read and write it as a string with <see cref="Get"/> and <see cref="Set"/>, rather than its bytes. Unlike
/// <see cref="Name"/>'s, a name too long for the buffer is refused, not cut: a cut name could match another bone.
/// </remarks>
public unsafe partial struct BoneName
{
    /// <summary>The most bytes a name may hold, its terminator aside.</summary>
    public const int MaxBytes = 255;

    /// <summary>The name; <c>null</c> when the buffer holds no terminator (it was written raw).</summary>
    public readonly string? Get()
    {
        fixed (byte* bytes = Bytes)
        {
            int length = 0;
            while (length <= MaxBytes && bytes[length] != 0)
                ++length;
            return length > MaxBytes ? null : Encoding.UTF8.GetString(bytes, length);
        }
    }

    /// <summary>Whether the buffer holds a terminator, so <see cref="Get"/> gives a name.</summary>
    public readonly bool IsTerminated => Get() is not null;

    /// <summary>
    /// Sets the name to <paramref name="name"/>. A name over <see cref="MaxBytes"/> bytes of UTF-8, or holding a zero
    /// character, is refused with an error to the engine's log, leaving the field unchanged.
    /// </summary>
    /// <returns>Whether it was set.</returns>
    public bool Set(ReadOnlySpan<char> name)
    {
        if (name.Contains('\0'))
        {
            Log.Error("BoneName.Set refused a name holding a zero character; the bone name is unchanged");
            return false;
        }
        int length = Encoding.UTF8.GetByteCount(name);
        if (length > MaxBytes)
        {
            Log.Error($"BoneName.Set refused a name of {length} bytes, over the {MaxBytes} a bone name holds; the bone " +
                      "name is unchanged");
            return false;
        }
        fixed (byte* bytes = Bytes)
        {
            var buffer = new Span<byte>(bytes, MaxBytes + 1);
            buffer.Clear();
            Encoding.UTF8.GetBytes(name, buffer);
        }
        return true;
    }

    /// <summary>A bone name holding <paramref name="name"/>, or the empty name if it is refused (logged).</summary>
    public static BoneName Of(ReadOnlySpan<char> name)
    {
        BoneName made = default;
        made.Set(name);
        return made;
    }
}
