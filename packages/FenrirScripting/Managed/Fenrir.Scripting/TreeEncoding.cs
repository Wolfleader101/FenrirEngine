using System.Buffers.Binary;
using System.Text;

namespace Fenrir.Scripting;

// The tree encoding in C# (ADR-0083 §2, ADR-0085 §2): FenrirSerialisation's Encoding.hpp is its definition, and these
// are its one managed reader and writer. The bytes are the tag 00 46 56 54, the version 01, then the root node: a tag
// byte (01 null, 02 false, 03 true, 04 signed, 05 unsigned, 06 single, 07 double, 08 string, 09 object, 0A array,
// 0B trivia) and what it says follows, every length and count unsigned LEB128, every number eight bytes (four for a
// single) little-endian. The writer writes no trivia, as the engine's pull to C# asks for none; the reader accepts it
// and drops the comments. Strings are UTF-8, and a lone surrogate is its three-byte form both ways, as C++ keeps it.

/// <summary>A buffer that is not one whole tree encoding: what was expected, and the byte offset it is about.</summary>
public sealed class TreeFormatException : FormatException
{
    /// <param name="offset">The byte the buffer went wrong at, counted from 0.</param>
    /// <param name="message">What was expected there, and what was found.</param>
    public TreeFormatException(long offset, string message) : base($"at byte {offset}, {message}") => Offset = offset;

    /// <summary>The byte the buffer went wrong at, counted from 0.</summary>
    public long Offset { get; }
}

/// <summary>Writes a <see cref="TreeNode"/> as the tree encoding, byte for byte as C++'s <c>EncodeTree</c> does with no trivia.</summary>
public static class TreeWriter
{
    internal static ReadOnlySpan<byte> Header => [0x00, (byte)'F', (byte)'V', (byte)'T', 0x01];

    /// <summary>The encoding of <paramref name="root"/>.</summary>
    public static byte[] Write(TreeNode root)
    {
        ArgumentNullException.ThrowIfNull(root);
        var output = new Output();
        output.Bytes(Header);
        output.Node(root);
        return output.ToArray();
    }

    private sealed class Output
    {
        private byte[] m_buffer = new byte[256];
        private int m_length;

        public byte[] ToArray() => m_buffer.AsSpan(0, m_length).ToArray();

        public void Node(TreeNode node)
        {
            switch (node.Kind)
            {
            case TreeKind.Null:
                Byte(0x01);
                break;
            case TreeKind.Bool:
                Byte(node.AsBool ? (byte)0x03 : (byte)0x02);
                break;
            case TreeKind.Signed:
                Byte(0x04);
                BinaryPrimitives.WriteInt64LittleEndian(Reserve(8), node.AsSigned);
                break;
            case TreeKind.Unsigned:
                Byte(0x05);
                BinaryPrimitives.WriteUInt64LittleEndian(Reserve(8), node.AsUnsigned);
                break;
            case TreeKind.Single:
                Byte(0x06);
                BinaryPrimitives.WriteUInt32LittleEndian(Reserve(4), BitConverter.SingleToUInt32Bits(node.AsSingle));
                break;
            case TreeKind.Double:
                Byte(0x07);
                BinaryPrimitives.WriteUInt64LittleEndian(Reserve(8), BitConverter.DoubleToUInt64Bits(node.AsDouble));
                break;
            case TreeKind.String:
                Byte(0x08);
                Text(node.AsString);
                break;
            case TreeKind.Object:
                Byte(0x09);
                Leb128((ulong)node.Members.Count);
                foreach (KeyValuePair<string, TreeNode> member in node.Members)
                {
                    Text(member.Key);
                    Node(member.Value);
                }
                break;
            case TreeKind.Array:
                Byte(0x0A);
                Leb128((ulong)node.Elements.Count);
                foreach (TreeNode element in node.Elements)
                    Node(element);
                break;
            }
        }

        public void Bytes(ReadOnlySpan<byte> bytes) => bytes.CopyTo(Reserve(bytes.Length));

        private void Byte(byte value) => Reserve(1)[0] = value;

        private void Leb128(ulong value)
        {
            do
            {
                byte next = (byte)(value & 0x7F);
                value >>= 7;
                Byte(value != 0 ? (byte)(next | 0x80) : next);
            } while (value != 0);
        }

        // A length, then the text as UTF-8: a surrogate pair as its four-byte form, a lone surrogate as its three.
        private void Text(string text)
        {
            int length = Utf8Length(text);
            Leb128((ulong)length);
            Span<byte> utf8 = Reserve(length);
            int at = 0;
            for (int i = 0; i < text.Length; ++i)
            {
                uint code = text[i];
                if (char.IsHighSurrogate(text[i]) && i + 1 < text.Length && char.IsLowSurrogate(text[i + 1]))
                    code = (uint)char.ConvertToUtf32(text[i], text[++i]);
                if (code < 0x80)
                {
                    utf8[at++] = (byte)code;
                }
                else if (code < 0x800)
                {
                    utf8[at++] = (byte)(0xC0 | (code >> 6));
                    utf8[at++] = (byte)(0x80 | (code & 0x3F));
                }
                else if (code < 0x10000)
                {
                    utf8[at++] = (byte)(0xE0 | (code >> 12));
                    utf8[at++] = (byte)(0x80 | ((code >> 6) & 0x3F));
                    utf8[at++] = (byte)(0x80 | (code & 0x3F));
                }
                else
                {
                    utf8[at++] = (byte)(0xF0 | (code >> 18));
                    utf8[at++] = (byte)(0x80 | ((code >> 12) & 0x3F));
                    utf8[at++] = (byte)(0x80 | ((code >> 6) & 0x3F));
                    utf8[at++] = (byte)(0x80 | (code & 0x3F));
                }
            }
        }

        private static int Utf8Length(string text)
        {
            int length = 0;
            for (int i = 0; i < text.Length; ++i)
            {
                char c = text[i];
                if (c < 0x80)
                    length += 1;
                else if (c < 0x800)
                    length += 2;
                else if (char.IsHighSurrogate(c) && i + 1 < text.Length && char.IsLowSurrogate(text[i + 1]))
                {
                    length += 4;
                    ++i;
                }
                else
                    length += 3;
            }
            return length;
        }

        private Span<byte> Reserve(int count)
        {
            if (m_length + count > m_buffer.Length)
                Array.Resize(ref m_buffer, System.Math.Max(m_buffer.Length * 2, m_length + count));
            Span<byte> span = m_buffer.AsSpan(m_length, count);
            m_length += count;
            return span;
        }
    }
}

/// <summary>
/// Reads the tree encoding into a <see cref="TreeNode"/>, refusing exactly what C++'s <c>DecodeTree</c> refuses, with a
/// <see cref="TreeFormatException"/> at the same byte offset. Comments in trivia nodes are checked and dropped.
/// </summary>
public static class TreeReader
{
    // As in text: nesting deeper than this many objects and arrays is refused.
    private const int MaxDepth = 256;

    /// <summary>The tree <paramref name="bytes"/> encode, from its tag byte.</summary>
    /// <exception cref="TreeFormatException">The bytes are not one whole tree encoding.</exception>
    public static TreeNode Read(ReadOnlySpan<byte> bytes)
    {
        var input = new Input(bytes);
        return input.Root();
    }

    /// <summary>The tree <paramref name="bytes"/> encode; false, with what was wrong, when they are not one whole encoding.</summary>
    public static bool TryRead(ReadOnlySpan<byte> bytes, out TreeNode? tree, out string? error)
    {
        try
        {
            tree = Read(bytes);
            error = null;
            return true;
        }
        catch (TreeFormatException exception)
        {
            tree = null;
            error = exception.Message;
            return false;
        }
    }

    private enum Holder
    {
        Root,
        MemberOrElement,
    }

    private ref struct Input
    {
        private readonly ReadOnlySpan<byte> m_bytes;
        private int m_position;

        public Input(ReadOnlySpan<byte> bytes)
        {
            m_bytes = bytes;
            m_position = 0;
        }

        public TreeNode Root()
        {
            ReadOnlySpan<byte> tag = TreeWriter.Header[..4];
            if (m_bytes.Length < tag.Length || !m_bytes[..tag.Length].SequenceEqual(tag))
                throw new TreeFormatException(0, "expected the tree encoding's tag 00 46 56 54");
            m_position = tag.Length;
            byte version = Byte("a version byte");
            if (version != 1)
                throw new TreeFormatException(tag.Length, $"expected version 1, found version {version}");
            TreeNode root = WithTrivia(0, Holder.Root);
            if (m_position != m_bytes.Length)
                throw new TreeFormatException(m_position,
                    $"expected the end of the buffer after the root node, found {m_bytes.Length - m_position} more bytes");
            return root;
        }

        private readonly int Left => m_bytes.Length - m_position;

        private byte Byte(string what)
        {
            if (Left == 0)
                throw new TreeFormatException(m_position, $"expected {what}, found the end of the buffer");
            return m_bytes[m_position++];
        }

        private ulong Leb128(string what)
        {
            int start = m_position;
            ulong value = 0;
            for (int shift = 0; ; shift += 7)
            {
                if (Left == 0)
                    throw new TreeFormatException(start, $"the buffer ends inside {what}");
                byte next = m_bytes[m_position++];
                // The tenth byte holds bit 63 alone: anything more, a continuation included, overflows.
                if (shift == 63 && next > 1)
                    throw new TreeFormatException(start, $"{what} overflows 64 bits");
                value |= (ulong)(next & 0x7F) << shift;
                if ((next & 0x80) == 0)
                    return value;
            }
        }

        private ReadOnlySpan<byte> Fixed(int size, string what)
        {
            if (Left < size)
                throw new TreeFormatException(m_position, $"the buffer ends inside {what} of {size} bytes, {Left} left");
            ReadOnlySpan<byte> bytes = m_bytes.Slice(m_position, size);
            m_position += size;
            return bytes;
        }

        private string Text(string what)
        {
            ulong length = Leb128($"the length of {what}");
            if (length > (ulong)Left)
                throw new TreeFormatException(m_position, $"{what} of {length} bytes runs past the end of the buffer, {Left} left");
            ReadOnlySpan<byte> bytes = m_bytes.Slice(m_position, (int)length);
            string text = Decode(bytes, out int bad);
            if (bad >= 0)
                throw new TreeFormatException(m_position + bad, $"expected UTF-8 in {what}, found the byte {bytes[bad]:X2}");
            m_position += bytes.Length;
            return text;
        }

        private void Comments(string what)
        {
            ulong count = Leb128($"a count of {what} comments");
            for (ulong i = 0; i < count; ++i)
                Text($"a {what} comment");
        }

        private TreeNode WithTrivia(int depth, Holder holder)
        {
            int tagOffset = m_position;
            byte tag = Byte("a node");
            if (tag != 0x0B)
                return Node(tag, tagOffset, depth);

            int triviaOffset = tagOffset;
            Comments("leading");
            int trailingOffset = m_position;
            ulong trailing = Leb128("a count of trailing comments");
            if (trailing > 1)
                throw new TreeFormatException(trailingOffset, $"expected at most one trailing comment, found {trailing}");
            if (trailing == 1)
            {
                if (holder == Holder.Root)
                    throw new TreeFormatException(triviaOffset, "expected no trailing comment on the root, found one");
                Text("a trailing comment");
            }
            ulong closing = Leb128("a count of closing comments");
            for (ulong i = 0; i < closing; ++i)
                Text("a closing comment");

            tagOffset = m_position;
            tag = Byte("the node the trivia is in front of");
            if (tag == 0x0B)
                throw new TreeFormatException(tagOffset, "expected the node the trivia is in front of, found a second trivia node");
            if (closing != 0 && tag != 0x09 && tag != 0x0A)
                throw new TreeFormatException(triviaOffset,
                    $"expected closing comments only in front of an object or an array, found them in front of node tag {tag:X2}");
            return Node(tag, tagOffset, depth);
        }

        private TreeNode Node(byte tag, int tagOffset, int depth)
        {
            switch (tag)
            {
            case 0x01:
                return TreeNode.Null;
            case 0x02:
                return TreeNode.From(false);
            case 0x03:
                return TreeNode.From(true);
            case 0x04:
                return TreeNode.From(BinaryPrimitives.ReadInt64LittleEndian(Fixed(8, "a signed integer")));
            case 0x05:
                return TreeNode.From(BinaryPrimitives.ReadUInt64LittleEndian(Fixed(8, "an unsigned integer")));
            case 0x06:
                return TreeNode.From(BitConverter.UInt32BitsToSingle(BinaryPrimitives.ReadUInt32LittleEndian(Fixed(4, "a single float"))));
            case 0x07:
                return TreeNode.From(BitConverter.UInt64BitsToDouble(BinaryPrimitives.ReadUInt64LittleEndian(Fixed(8, "a double float"))));
            case 0x08:
                return TreeNode.From(Text("a string"));
            case 0x09:
            {
                Deeper(tagOffset, depth);
                ulong count = Leb128("a member count");
                TreeNode obj = TreeNode.NewObject();
                for (ulong i = 0; i < count; ++i)
                {
                    string key = Text("a key");
                    obj.Add(key, WithTrivia(depth + 1, Holder.MemberOrElement));
                }
                return obj;
            }
            case 0x0A:
            {
                Deeper(tagOffset, depth);
                ulong count = Leb128("an element count");
                TreeNode array = TreeNode.NewArray();
                for (ulong i = 0; i < count; ++i)
                    array.Add(WithTrivia(depth + 1, Holder.MemberOrElement));
                return array;
            }
            default:
                throw new TreeFormatException(tagOffset, $"expected a node tag, found the unknown tag {tag:X2}");
            }
        }

        private static void Deeper(int tagOffset, int depth)
        {
            if (depth + 1 > MaxDepth)
                throw new TreeFormatException(tagOffset, $"expected nesting of at most {MaxDepth} objects and arrays, found deeper");
        }

        // The text of well-formed UTF-8, a surrogate's three-byte form passing as C++'s reader lets it; `bad` is the
        // index of the first byte that is not, or -1.
        private static string Decode(ReadOnlySpan<byte> bytes, out int bad)
        {
            var text = new StringBuilder(bytes.Length);
            for (int i = 0; i < bytes.Length;)
            {
                byte lead = bytes[i];
                int length;
                uint min;
                if (lead < 0x80)
                {
                    text.Append((char)lead);
                    ++i;
                    continue;
                }
                if (lead >= 0xC2 && lead < 0xE0)
                    (length, min) = (2, 0x80u);
                else if (lead >= 0xE0 && lead < 0xF0)
                    (length, min) = (3, 0x800u);
                else if (lead >= 0xF0 && lead < 0xF5)
                    (length, min) = (4, 0x10000u);
                else
                {
                    bad = i;
                    return "";
                }
                if (bytes.Length - i < length)
                {
                    bad = i;
                    return "";
                }
                uint code = (uint)(lead & (0xFF >> (length + 1)));
                for (int k = 1; k < length; ++k)
                {
                    byte next = bytes[i + k];
                    if ((next & 0xC0) != 0x80)
                    {
                        bad = i;
                        return "";
                    }
                    code = (code << 6) | (uint)(next & 0x3F);
                }
                if (code < min || code > 0x10FFFF)
                {
                    bad = i;
                    return "";
                }
                if (code < 0x10000)
                    text.Append((char)code);
                else
                    text.Append(char.ConvertFromUtf32((int)code));
                i += length;
            }
            bad = -1;
            return text.ToString();
        }
    }
}
