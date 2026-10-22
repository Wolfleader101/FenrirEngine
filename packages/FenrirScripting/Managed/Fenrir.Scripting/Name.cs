using System.Text;

namespace Fenrir.Scripting;

/// <remarks>Read and write it as a string with <see cref="GetText"/> and <see cref="SetText"/>, rather than its bytes.</remarks>
public unsafe partial struct Name
{
    /// <summary>Bytes in the buffer, including the terminator.</summary>
    public const int Capacity = 256;

    /// <summary>The name as a string.</summary>
    public readonly string GetText()
    {
        fixed (byte* bytes = Text)
        {
            int length = 0;
            while (length < Capacity && bytes[length] != 0)
                ++length;
            return Encoding.UTF8.GetString(bytes, length);
        }
    }

    /// <summary>
    /// Replaces the name. Text longer than the buffer is cut at a character boundary, so it never ends in half of a
    /// UTF-8 sequence.
    /// </summary>
    public void SetText(string? text)
    {
        text ??= "";
        ReadOnlySpan<char> source = text.AsSpan(0, Math.Min(text.Length, Capacity));
        Span<byte> encoded = stackalloc byte[Encoding.UTF8.GetMaxByteCount(source.Length)];
        int length = Encoding.UTF8.GetBytes(source, encoded);
        if (length > Capacity - 1)
        {
            length = Capacity - 1;
            // Step back off a UTF-8 continuation byte so the cut falls between characters.
            while (length > 0 && (encoded[length] & 0xC0) == 0x80)
                --length;
        }
        fixed (byte* bytes = Text)
        {
            var buffer = new Span<byte>(bytes, Capacity);
            buffer.Clear();
            encoded[..length].CopyTo(buffer);
        }
    }
}

/// <summary>Reads and writes a <see cref="Name"/> through its <see cref="Ref{T}"/> as a string.</summary>
public static class NameRefExtensions
{
    extension(Ref<Name> self)
    {
        /// <summary>The name as a string.</summary>
        public string Text
        {
            get => self.Value.GetText();
            set => self.Value.SetText(value);
        }
    }
}
