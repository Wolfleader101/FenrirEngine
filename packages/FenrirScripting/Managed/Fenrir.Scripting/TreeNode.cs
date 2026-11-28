using System.Globalization;
using System.Text;

namespace Fenrir.Scripting;

/// <summary>
/// What a <see cref="TreeNode"/> is: one of the six JSON5 values, with a number in its own width, as FenrirSerialisation's
/// value tree holds it (ADR-0079).
/// </summary>
public enum TreeKind
{
    Null,
    Bool,
    /// <summary>A signed 64-bit integer.</summary>
    Signed,
    /// <summary>An unsigned 64-bit integer, for a value no signed one holds or a field of an unsigned kind.</summary>
    Unsigned,
    /// <summary>A single float.</summary>
    Single,
    /// <summary>A double float.</summary>
    Double,
    String,
    /// <summary>Members in order, each a key and a value. A key may appear twice; the first is the one found.</summary>
    Object,
    /// <summary>Elements in order.</summary>
    Array,
}

/// <summary>
/// A value tree in C#: the form a data asset crosses to and from the engine in, through <see cref="TreeReader"/> and
/// <see cref="TreeWriter"/> over the tree encoding (ADR-0083, ADR-0085). A data asset's codecs write one and read one,
/// and an <see cref="UpgraderAttribute"/> method takes one and returns one. It holds no comments.
/// </summary>
/// <remarks>
/// A scalar never changes once made. An object or an array is changed in place through <see cref="Add(string, TreeNode)"/>,
/// <see cref="Set"/>, <see cref="Remove"/> and <see cref="Add(TreeNode)"/>.
/// </remarks>
public sealed class TreeNode
{
    private readonly List<KeyValuePair<string, TreeNode>>? m_members;
    private readonly List<TreeNode>? m_elements;
    // A bool, an integer of either sign, or a float's bits: whichever Kind says.
    private readonly ulong m_bits;
    private readonly string? m_text;

    private static readonly TreeNode s_null = new(TreeKind.Null, 0, null);
    private static readonly TreeNode s_false = new(TreeKind.Bool, 0, null);
    private static readonly TreeNode s_true = new(TreeKind.Bool, 1, null);

    private TreeNode(TreeKind kind, ulong bits, string? text)
    {
        Kind = kind;
        m_bits = bits;
        m_text = text;
        if (kind == TreeKind.Object)
            m_members = new List<KeyValuePair<string, TreeNode>>();
        else if (kind == TreeKind.Array)
            m_elements = new List<TreeNode>();
    }

    /// <summary>What this node is.</summary>
    public TreeKind Kind { get; }

    /// <summary><c>null</c>.</summary>
    public static TreeNode Null => s_null;

    public static TreeNode From(bool value) => value ? s_true : s_false;

    /// <summary>A signed integer.</summary>
    public static TreeNode From(long value) => new(TreeKind.Signed, unchecked((ulong)value), null);

    /// <summary>An unsigned integer.</summary>
    public static TreeNode From(ulong value) => new(TreeKind.Unsigned, value, null);

    /// <summary>A single float, its bits as they are, so every <c>NaN</c> survives.</summary>
    public static TreeNode From(float value) => new(TreeKind.Single, BitConverter.SingleToUInt32Bits(value), null);

    /// <summary>A double float, its bits as they are.</summary>
    public static TreeNode From(double value) => new(TreeKind.Double, BitConverter.DoubleToUInt64Bits(value), null);

    /// <summary>A string; <c>null</c> is the empty string.</summary>
    public static TreeNode From(string? value) => new(TreeKind.String, 0, value ?? "");

    /// <summary>A new object with no members.</summary>
    public static TreeNode NewObject() => new(TreeKind.Object, 0, null);

    /// <summary>A new array with no elements.</summary>
    public static TreeNode NewArray() => new(TreeKind.Array, 0, null);

    /// <summary>Whether this is a number of any width.</summary>
    public bool IsNumber => Kind is TreeKind.Signed or TreeKind.Unsigned or TreeKind.Single or TreeKind.Double;

    /// <summary>The value of a <see cref="TreeKind.Bool"/>.</summary>
    public bool AsBool => Expect(TreeKind.Bool).m_bits != 0;

    /// <summary>The value of a <see cref="TreeKind.Signed"/>.</summary>
    public long AsSigned => unchecked((long)Expect(TreeKind.Signed).m_bits);

    /// <summary>The value of a <see cref="TreeKind.Unsigned"/>.</summary>
    public ulong AsUnsigned => Expect(TreeKind.Unsigned).m_bits;

    /// <summary>The value of a <see cref="TreeKind.Single"/>.</summary>
    public float AsSingle => BitConverter.UInt32BitsToSingle((uint)Expect(TreeKind.Single).m_bits);

    /// <summary>The value of a <see cref="TreeKind.Double"/>.</summary>
    public double AsDouble => BitConverter.UInt64BitsToDouble(Expect(TreeKind.Double).m_bits);

    /// <summary>The text of a <see cref="TreeKind.String"/>.</summary>
    public string AsString => Expect(TreeKind.String).m_text!;

    /// <summary>The integer this number holds as a <c>long</c>: a signed one, or an unsigned one a <c>long</c> holds.</summary>
    public bool TryGetInt64(out long value)
    {
        if (Kind == TreeKind.Signed || (Kind == TreeKind.Unsigned && m_bits <= long.MaxValue))
        {
            value = unchecked((long)m_bits);
            return true;
        }
        value = 0;
        return false;
    }

    /// <summary>The integer this number holds as a <c>ulong</c>: an unsigned one, or a signed one that is not negative.</summary>
    public bool TryGetUInt64(out ulong value)
    {
        if (Kind == TreeKind.Unsigned || (Kind == TreeKind.Signed && (long)m_bits >= 0))
        {
            value = m_bits;
            return true;
        }
        value = 0;
        return false;
    }

    /// <summary>Any number as a <c>double</c>, an integer rounded to the nearest.</summary>
    public bool TryGetDouble(out double value)
    {
        switch (Kind)
        {
        case TreeKind.Signed:
            value = (long)m_bits;
            return true;
        case TreeKind.Unsigned:
            value = m_bits;
            return true;
        case TreeKind.Single:
            value = BitConverter.UInt32BitsToSingle((uint)m_bits);
            return true;
        case TreeKind.Double:
            value = BitConverter.UInt64BitsToDouble(m_bits);
            return true;
        default:
            value = 0;
            return false;
        }
    }

    /// <summary>An object's members, in order.</summary>
    public IReadOnlyList<KeyValuePair<string, TreeNode>> Members => Expect(TreeKind.Object).m_members!;

    /// <summary>An array's elements, in order.</summary>
    public IReadOnlyList<TreeNode> Elements => Expect(TreeKind.Array).m_elements!;

    /// <summary>The number of an object's members or an array's elements.</summary>
    public int Count => Kind switch
    {
        TreeKind.Object => m_members!.Count,
        TreeKind.Array => m_elements!.Count,
        _ => throw WrongKind("an object or an array"),
    };

    /// <summary>The first member of an object named <paramref name="key"/>; null when there is none.</summary>
    public TreeNode? Find(string key)
    {
        foreach (KeyValuePair<string, TreeNode> member in Expect(TreeKind.Object).m_members!)
            if (member.Key == key)
                return member.Value;
        return null;
    }

    /// <summary>Adds a member at the end of an object, and gives this object back.</summary>
    public TreeNode Add(string key, TreeNode value)
    {
        Expect(TreeKind.Object).m_members!.Add(new KeyValuePair<string, TreeNode>(key, value ?? throw new ArgumentNullException(nameof(value))));
        return this;
    }

    /// <summary>Gives an object's first member named <paramref name="key"/> the value, or adds one at the end.</summary>
    public void Set(string key, TreeNode value)
    {
        List<KeyValuePair<string, TreeNode>> members = Expect(TreeKind.Object).m_members!;
        int index = IndexOf(members, key);
        var member = new KeyValuePair<string, TreeNode>(key, value ?? throw new ArgumentNullException(nameof(value)));
        if (index < 0)
            members.Add(member);
        else
            members[index] = member;
    }

    /// <summary>Removes an object's first member named <paramref name="key"/>; false when there is none.</summary>
    public bool Remove(string key)
    {
        List<KeyValuePair<string, TreeNode>> members = Expect(TreeKind.Object).m_members!;
        int index = IndexOf(members, key);
        if (index < 0)
            return false;
        members.RemoveAt(index);
        return true;
    }

    /// <summary>
    /// Renames an object's first member named <paramref name="from"/>, keeping its place; false when there is none. An
    /// upgrader uses it for a field that changed its name without a <see cref="FormerlyAttribute"/>.
    /// </summary>
    public bool Rename(string from, string to)
    {
        List<KeyValuePair<string, TreeNode>> members = Expect(TreeKind.Object).m_members!;
        int index = IndexOf(members, from);
        if (index < 0)
            return false;
        members[index] = new KeyValuePair<string, TreeNode>(to, members[index].Value);
        return true;
    }

    /// <summary>Adds an element at the end of an array, and gives this array back.</summary>
    public TreeNode Add(TreeNode element)
    {
        Expect(TreeKind.Array).m_elements!.Add(element ?? throw new ArgumentNullException(nameof(element)));
        return this;
    }

    /// <summary>
    /// Whether <paramref name="other"/> is the same tree: the same kinds, values and member order, a float compared by its
    /// bits, so a <c>-0</c> differs from a <c>0</c> and a <c>NaN</c> equals itself.
    /// </summary>
    public bool SameAs(TreeNode other)
    {
        if (ReferenceEquals(this, other))
            return true;
        if (other is null || Kind != other.Kind)
            return false;
        switch (Kind)
        {
        case TreeKind.String:
            return string.Equals(m_text, other.m_text, StringComparison.Ordinal);
        case TreeKind.Object:
            if (m_members!.Count != other.m_members!.Count)
                return false;
            for (int i = 0; i < m_members.Count; ++i)
                if (m_members[i].Key != other.m_members[i].Key || !m_members[i].Value.SameAs(other.m_members[i].Value))
                    return false;
            return true;
        case TreeKind.Array:
            if (m_elements!.Count != other.m_elements!.Count)
                return false;
            for (int i = 0; i < m_elements.Count; ++i)
                if (!m_elements[i].SameAs(other.m_elements[i]))
                    return false;
            return true;
        default:
            return m_bits == other.m_bits;
        }
    }

    /// <summary>A copy of this tree that shares no object or array with it.</summary>
    public TreeNode Clone()
    {
        switch (Kind)
        {
        case TreeKind.Object:
            TreeNode obj = NewObject();
            foreach (KeyValuePair<string, TreeNode> member in m_members!)
                obj.m_members!.Add(new KeyValuePair<string, TreeNode>(member.Key, member.Value.Clone()));
            return obj;
        case TreeKind.Array:
            TreeNode array = NewArray();
            foreach (TreeNode element in m_elements!)
                array.m_elements!.Add(element.Clone());
            return array;
        default:
            return this;
        }
    }

    /// <summary>
    /// The tree in one line, for a test or a log: numbers marked by width (<c>5</c>, <c>5u</c>, <c>0.5f</c>, <c>0.5</c>),
    /// strings quoted with C# escapes, and keys quoted.
    /// </summary>
    public override string ToString()
    {
        var text = new StringBuilder();
        Append(text);
        return text.ToString();
    }

    private void Append(StringBuilder text)
    {
        switch (Kind)
        {
        case TreeKind.Null:
            text.Append("null");
            break;
        case TreeKind.Bool:
            text.Append(m_bits != 0 ? "true" : "false");
            break;
        case TreeKind.Signed:
            text.Append(((long)m_bits).ToString(CultureInfo.InvariantCulture));
            break;
        case TreeKind.Unsigned:
            text.Append(m_bits.ToString(CultureInfo.InvariantCulture)).Append('u');
            break;
        case TreeKind.Single:
            text.Append(AsSingle.ToString("R", CultureInfo.InvariantCulture)).Append('f');
            break;
        case TreeKind.Double:
            text.Append(AsDouble.ToString("R", CultureInfo.InvariantCulture));
            break;
        case TreeKind.String:
            AppendQuoted(text, m_text!);
            break;
        case TreeKind.Object:
            text.Append('{');
            for (int i = 0; i < m_members!.Count; ++i)
            {
                if (i > 0)
                    text.Append(", ");
                AppendQuoted(text, m_members[i].Key);
                text.Append(": ");
                m_members[i].Value.Append(text);
            }
            text.Append('}');
            break;
        case TreeKind.Array:
            text.Append('[');
            for (int i = 0; i < m_elements!.Count; ++i)
            {
                if (i > 0)
                    text.Append(", ");
                m_elements[i].Append(text);
            }
            text.Append(']');
            break;
        }
    }

    private static void AppendQuoted(StringBuilder text, string value)
    {
        text.Append('"');
        foreach (char c in value)
        {
            if (c == '"' || c == '\\')
                text.Append('\\').Append(c);
            else if (c < 0x20)
                text.Append("\\u").Append(((int)c).ToString("X4", CultureInfo.InvariantCulture));
            else
                text.Append(c);
        }
        text.Append('"');
    }

    private static int IndexOf(List<KeyValuePair<string, TreeNode>> members, string key)
    {
        for (int i = 0; i < members.Count; ++i)
            if (members[i].Key == key)
                return i;
        return -1;
    }

    private TreeNode Expect(TreeKind kind) => Kind == kind ? this : throw WrongKind(kind.ToString().ToLowerInvariant());

    private InvalidOperationException WrongKind(string expected) =>
        new($"The tree node is {Kind.ToString().ToLowerInvariant()}, not {expected}.");
}
