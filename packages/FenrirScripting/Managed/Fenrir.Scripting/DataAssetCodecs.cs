using System.ComponentModel;
using System.Reflection;

namespace Fenrir.Scripting;

/// <summary>
/// What every data asset type's codecs have in common, whatever the type: its canonical name, version and base, its
/// description entry, the tree of a value and a value from a tree, and its upgraders by version. The generated codecs of
/// a C# data asset class, and those that come with the generated class of a C++ one, are <see cref="DataAssetCodec{T}"/>s;
/// <see cref="DataAssetCodecs"/> finds them by type or name.
/// </summary>
public abstract class DataAssetCodec
{
    private protected DataAssetCodec(Type type, string name, int version, string? baseName, IReadOnlyList<int> upgraderVersions)
    {
        Type = type;
        Name = name;
        Version = version;
        BaseName = baseName;
        UpgraderVersions = upgraderVersions;
    }

    /// <summary>The C# class.</summary>
    public Type Type { get; }

    /// <summary>The canonical name: the full C# name of a C# class, the C++ name of a generated one.</summary>
    public string Name { get; }

    /// <summary><see cref="VersionAttribute"/>'s: the version its trees are written at.</summary>
    public int Version { get; }

    /// <summary>The canonical name of the data asset type it extends; null when it extends <c>object</c>.</summary>
    public string? BaseName { get; }

    /// <summary>The versions its upgraders take trees at, lowest first.</summary>
    public IReadOnlyList<int> UpgraderVersions { get; }

    /// <summary>
    /// The description entry: name, version, former names, base, flags, fields with their kinds, targets and metadata,
    /// the structs they hold, the upgrader versions, and the default as a tree of every field of a <c>new T()</c>.
    /// </summary>
    public abstract TreeNode Entry();

    /// <summary>The tree of <paramref name="value"/>, which must be of <see cref="Type"/>, with fields at their defaults left out.</summary>
    public abstract TreeNode ToTreeUntyped(object value);

    /// <summary>A new value of <see cref="Type"/> read from <paramref name="tree"/>.</summary>
    public abstract object FromTreeUntyped(TreeNode tree);

    /// <summary>The tree <paramref name="tree"/>, written at <paramref name="fromVersion"/>, upgraded to the next version.</summary>
    /// <exception cref="KeyNotFoundException">The type has no upgrader from that version.</exception>
    public abstract TreeNode Upgrade(int fromVersion, TreeNode tree);
}

/// <summary>
/// The codecs of the data asset class <typeparamref name="T"/> (ADR-0083 §1): its tree, base fields first, with every
/// field at its default left out as C++ writes a value (ADR-0075); and a new <typeparamref name="T"/> read from a tree,
/// a field the tree lacks or holds in the wrong form keeping its default, and a key no field has dropped (ADR-0077).
/// </summary>
public sealed class DataAssetCodec<T> : DataAssetCodec where T : class
{
    private readonly Func<TreeNode> m_entry;
    private readonly Action<TreeNode, T, T?> m_writeFields;
    private readonly Action<TreeNode, T> m_readFields;
    private readonly Func<T> m_make;
    private readonly Dictionary<int, Func<TreeNode, TreeNode>> m_upgraders;

    /// <summary>Made by generated code; a script never makes one.</summary>
    /// <param name="writeFields">
    /// Adds the type's fields, its base's first, to an object: those that differ from the defaults, or every one when the
    /// defaults are null.
    /// </param>
    /// <param name="readFields">Reads the fields the object holds, its base's first, into a value.</param>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public DataAssetCodec(string name, int version, string? baseName, Func<TreeNode> entry, Action<TreeNode, T, T?> writeFields,
        Action<TreeNode, T> readFields, Func<T> make, IEnumerable<KeyValuePair<int, Func<TreeNode, TreeNode>>>? upgraders)
        : this(name, version, baseName, entry, writeFields, readFields, make, ToDictionary(upgraders))
    {
    }

    private DataAssetCodec(string name, int version, string? baseName, Func<TreeNode> entry, Action<TreeNode, T, T?> writeFields,
        Action<TreeNode, T> readFields, Func<T> make, Dictionary<int, Func<TreeNode, TreeNode>> upgraders)
        : base(typeof(T), name, version, baseName, upgraders.Keys.OrderBy(static v => v).ToArray())
    {
        m_entry = entry;
        m_writeFields = writeFields;
        m_readFields = readFields;
        m_make = make;
        m_upgraders = upgraders;
    }

    public override TreeNode Entry() => m_entry();

    /// <summary>The tree of <paramref name="value"/>, each field equal to a <c>new T()</c>'s left out, as C++ writes it.</summary>
    public TreeNode ToTree(T value) => ToTree(value, m_make());

    /// <summary>
    /// The tree of <paramref name="value"/>, each field equal to <paramref name="defaults"/>' left out; every field when
    /// <paramref name="defaults"/> is null.
    /// </summary>
    public TreeNode ToTree(T value, T? defaults)
    {
        ArgumentNullException.ThrowIfNull(value);
        TreeNode tree = TreeNode.NewObject();
        m_writeFields(tree, value, defaults);
        return tree;
    }

    /// <summary>
    /// A new <typeparamref name="T"/> read from <paramref name="tree"/>: a field it lacks, or holds in a form the field
    /// cannot take, keeps its default, and a key no field or former name has is dropped. A tree that is not an object
    /// gives the default value.
    /// </summary>
    public T FromTree(TreeNode tree)
    {
        ArgumentNullException.ThrowIfNull(tree);
        T value = m_make();
        if (tree.Kind == TreeKind.Object)
            m_readFields(tree, value);
        return value;
    }

    /// <summary>Adds the fields of <paramref name="value"/> to <paramref name="tree"/>: what a subtype's codec calls first.</summary>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public void WriteFields(TreeNode tree, T value, T? defaults) => m_writeFields(tree, value, defaults);

    /// <summary>Reads the fields of <paramref name="value"/> from <paramref name="tree"/>: what a subtype's codec calls first.</summary>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public void ReadFields(TreeNode tree, T value) => m_readFields(tree, value);

    public override TreeNode ToTreeUntyped(object value) => ToTree((T)value);

    public override object FromTreeUntyped(TreeNode tree) => FromTree(tree);

    public override TreeNode Upgrade(int fromVersion, TreeNode tree)
    {
        if (!m_upgraders.TryGetValue(fromVersion, out Func<TreeNode, TreeNode>? upgrader))
            throw new KeyNotFoundException($"The data asset type {Name} has no upgrader from version {fromVersion}.");
        return upgrader(tree) ?? throw new InvalidOperationException($"The upgrader of {Name} from version {fromVersion} returned null.");
    }

    private static Dictionary<int, Func<TreeNode, TreeNode>> ToDictionary(IEnumerable<KeyValuePair<int, Func<TreeNode, TreeNode>>>? upgraders)
    {
        var byVersion = new Dictionary<int, Func<TreeNode, TreeNode>>();
        if (upgraders != null)
            foreach (KeyValuePair<int, Func<TreeNode, TreeNode>> upgrader in upgraders)
                byVersion.Add(upgrader.Key, upgrader.Value);
        return byVersion;
    }
}

/// <summary>
/// The codecs of a struct a data asset holds that the script does not define: the generated mirror of a C++ struct,
/// whose codecs come with the contract that has it. <see cref="Name"/> is the C++ type's canonical name.
/// </summary>
[EditorBrowsable(EditorBrowsableState.Never)]
public sealed class ValueCodec<T> where T : struct
{
    private readonly Func<T, T, bool, TreeNode> m_toTree;
    private readonly Func<TreeNode, T, T> m_fromTree;

    /// <param name="toTree">The tree of a value: its fields that differ from the defaults, or every one when told to.</param>
    /// <param name="fromTree">A value read from a tree, what it lacks or cannot be read kept from the fallback.</param>
    public ValueCodec(string name, Func<T, T, bool, TreeNode> toTree, Func<TreeNode, T, T> fromTree)
    {
        Name = name;
        m_toTree = toTree;
        m_fromTree = fromTree;
    }

    public string Name { get; }

    public TreeNode ToTree(T value, T defaults, bool full) => m_toTree(value, defaults, full);

    public T FromTree(TreeNode node, T fallback) => m_fromTree(node, fallback);
}

/// <summary>
/// Every data asset type's codecs, keyed by its C# <see cref="Type"/>: filled by each script assembly's generated
/// <c>DataAssetTable</c> and by each contract that has generated classes of C++ types.
/// </summary>
public static class DataAssetCodecs
{
    private static readonly object s_lock = new();
    private static readonly Dictionary<Type, DataAssetCodec> s_codecs = new();
    private static readonly Dictionary<Type, object> s_values = new();

    /// <summary>Adds <paramref name="codec"/>, in place of one its type already has.</summary>
    public static void Register(DataAssetCodec codec)
    {
        ArgumentNullException.ThrowIfNull(codec);
        lock (s_lock)
            s_codecs[codec.Type] = codec;
    }

    /// <summary>Adds the codecs of a struct mirror, in place of any it already has.</summary>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static void RegisterValue<T>(ValueCodec<T> codec) where T : struct
    {
        ArgumentNullException.ThrowIfNull(codec);
        lock (s_lock)
            s_values[typeof(T)] = codec;
    }

    /// <summary>The codecs of <typeparamref name="T"/>; null when it has none.</summary>
    public static DataAssetCodec<T>? Find<T>() where T : class
    {
        lock (s_lock)
            return s_codecs.TryGetValue(typeof(T), out DataAssetCodec? codec) ? (DataAssetCodec<T>)codec : null;
    }

    /// <summary>The codecs of <paramref name="type"/>; null when it has none.</summary>
    public static DataAssetCodec? Find(Type type)
    {
        lock (s_lock)
            return s_codecs.TryGetValue(type, out DataAssetCodec? codec) ? codec : null;
    }

    /// <summary>The codecs of the type named <paramref name="name"/>, canonically; null when none has it.</summary>
    public static DataAssetCodec? Find(string name)
    {
        lock (s_lock)
            return s_codecs.Values.FirstOrDefault(codec => codec.Name == name);
    }

    /// <summary>Every codec, in no order.</summary>
    public static IReadOnlyList<DataAssetCodec> All
    {
        get
        {
            lock (s_lock)
                return s_codecs.Values.ToArray();
        }
    }

    /// <summary>
    /// Drops the codecs of every type in <paramref name="assembly"/>, so nothing here keeps a script assembly that is being
    /// unloaded alive; how many it dropped.
    /// </summary>
    public static int Forget(Assembly assembly)
    {
        ArgumentNullException.ThrowIfNull(assembly);
        lock (s_lock)
        {
            Type[] codecs = s_codecs.Keys.Where(type => type.Assembly == assembly).ToArray();
            foreach (Type type in codecs)
                s_codecs.Remove(type);
            Type[] values = s_values.Keys.Where(type => type.Assembly == assembly).ToArray();
            foreach (Type type in values)
                s_values.Remove(type);
            return codecs.Length + values.Length;
        }
    }

    /// <summary>The codecs of <typeparamref name="T"/>, which a subtype's codecs walk first.</summary>
    /// <exception cref="InvalidOperationException">They are not registered.</exception>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static DataAssetCodec<T> Require<T>() where T : class =>
        Find<T>() ?? throw new InvalidOperationException(
            $"The data asset type {typeof(T).FullName} has no codecs: its script's DataAssetTable, or the contract with its generated class, has not registered them.");

    /// <summary>The codecs of the struct mirror <typeparamref name="T"/>.</summary>
    /// <exception cref="InvalidOperationException">They are not registered.</exception>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static ValueCodec<T> RequireValue<T>() where T : struct
    {
        lock (s_lock)
            if (s_values.TryGetValue(typeof(T), out object? codec))
                return (ValueCodec<T>)codec;
        throw new InvalidOperationException(
            $"The struct {typeof(T).FullName} has no codecs: the contract with its generated mirror has not registered them.");
    }
}

/// <summary>
/// How a data asset's <c>reference</c> field turns into the path a tree holds and back (ADR-0076): the asset store's,
/// which FenrirAssets' scripting contract installs. With none installed, a reference is written <c>null</c> and read as
/// the empty handle, as the C++ bridge does with no resolvers.
/// </summary>
public interface ITreeReferenceResolver
{
    /// <summary>The path of the asset of type <paramref name="target"/> the handle names; null or empty for none.</summary>
    string? PathOf(uint index, uint generation, string target);

    /// <summary>The handle of the asset of type <paramref name="target"/> at <paramref name="path"/>; false when there is none.</summary>
    bool TryResolve(string path, string target, out uint index, out uint generation);
}

/// <summary>Where the data asset codecs find their <see cref="ITreeReferenceResolver"/>.</summary>
public static class TreeReferences
{
    private static volatile ITreeReferenceResolver? s_resolver;

    /// <summary>The resolver the codecs ask; null for none.</summary>
    public static ITreeReferenceResolver? Resolver
    {
        get => s_resolver;
        set => s_resolver = value;
    }
}

/// <summary>
/// The pieces the generated data asset codecs are written with: one read and one write per kind, in the forms C++'s
/// bridge reads and writes (ADR-0079). A read given a node it cannot take gives the fallback, the field's default.
/// </summary>
[EditorBrowsable(EditorBrowsableState.Never)]
public static class TreeCodec
{
    /// <summary>The member of <paramref name="tree"/> named <paramref name="key"/>, else the first of <paramref name="formerly"/> it has.</summary>
    public static TreeNode? Find(TreeNode tree, string key, params string[] formerly)
    {
        if (tree.Find(key) is { } current)
            return current;
        foreach (string name in formerly)
            if (tree.Find(name) is { } former)
                return former;
        return null;
    }

    public static long ReadSigned(TreeNode node, long min, long max, long fallback) =>
        node.TryGetInt64(out long value) && value >= min && value <= max ? value : fallback;

    public static ulong ReadUnsigned(TreeNode node, ulong max, ulong fallback) =>
        node.TryGetUInt64(out ulong value) && value <= max ? value : fallback;

    /// <summary>Any number, but a finite one beyond a float's range, which takes the fallback as C++ does.</summary>
    public static float ReadSingle(TreeNode node, float fallback)
    {
        if (node.Kind == TreeKind.Single)
            return node.AsSingle;
        if (!node.TryGetDouble(out double value))
            return fallback;
        if (double.IsFinite(value) && System.Math.Abs(value) > float.MaxValue)
            return fallback;
        return (float)value;
    }

    public static double ReadDouble(TreeNode node, double fallback) => node.TryGetDouble(out double value) ? value : fallback;

    public static bool ReadBool(TreeNode node, bool fallback) => node.Kind == TreeKind.Bool ? node.AsBool : fallback;

    public static string ReadString(TreeNode node, string fallback) => node.Kind == TreeKind.String ? node.AsString : fallback;

    /// <summary>An enum is written as its number, in its underlying type's sign; read from that number, or an enumerator's name.</summary>
    public static TreeNode WriteEnum<TEnum>(TEnum value) where TEnum : struct, Enum =>
        IsUnsigned<TEnum>() ? TreeNode.From(Convert.ToUInt64(value)) : TreeNode.From(Convert.ToInt64(value));

    public static TEnum ReadEnum<TEnum>(TreeNode node, TEnum fallback) where TEnum : struct, Enum
    {
        if (node.Kind == TreeKind.String)
            return Enum.GetNames<TEnum>().Contains(node.AsString, StringComparer.Ordinal) ? Enum.Parse<TEnum>(node.AsString) : fallback;
        Type underlying = Enum.GetUnderlyingType(typeof(TEnum));
        if (IsUnsigned<TEnum>())
        {
            ulong max = underlying == typeof(byte) ? byte.MaxValue : underlying == typeof(ushort) ? ushort.MaxValue :
                underlying == typeof(uint) ? uint.MaxValue : ulong.MaxValue;
            return node.TryGetUInt64(out ulong value) && value <= max ? (TEnum)Enum.ToObject(typeof(TEnum), value) : fallback;
        }
        (long min, long max) range = underlying == typeof(sbyte) ? (sbyte.MinValue, sbyte.MaxValue) :
            underlying == typeof(short) ? (short.MinValue, short.MaxValue) :
            underlying == typeof(int) ? (int.MinValue, int.MaxValue) : (long.MinValue, long.MaxValue);
        return node.TryGetInt64(out long signed) && signed >= range.min && signed <= range.max
            ? (TEnum)Enum.ToObject(typeof(TEnum), signed)
            : fallback;
    }

    private static bool IsUnsigned<TEnum>() where TEnum : struct, Enum
    {
        Type underlying = Enum.GetUnderlyingType(typeof(TEnum));
        return underlying == typeof(byte) || underlying == typeof(ushort) || underlying == typeof(uint) || underlying == typeof(ulong);
    }

    /// <summary>A list as an array, each element written by <paramref name="element"/>; a null list as an empty one.</summary>
    public static TreeNode WriteList<TElement>(List<TElement>? list, bool full, Func<TElement, bool, TreeNode> element)
    {
        TreeNode array = TreeNode.NewArray();
        if (list != null)
            foreach (TElement item in list)
                array.Add(element(item, full));
        return array;
    }

    /// <summary>A new list of an array's elements, each read over <paramref name="elementDefault"/>; the fallback for any other node.</summary>
    public static List<TElement>? ReadList<TElement>(TreeNode node, List<TElement>? fallback, TElement elementDefault,
        Func<TreeNode, TElement, TElement> element)
    {
        if (node.Kind != TreeKind.Array)
            return fallback;
        var list = new List<TElement>(node.Count);
        foreach (TreeNode item in node.Elements)
            list.Add(element(item, elementDefault));
        return list;
    }

    /// <summary><c>null</c> is disengaged; anything else is the held value, read over the fallback's or <paramref name="heldDefault"/>.</summary>
    public static THeld? ReadOptional<THeld>(TreeNode node, THeld? fallback, THeld heldDefault, Func<TreeNode, THeld, THeld> held)
        where THeld : struct =>
        node.Kind == TreeKind.Null ? null : held(node, fallback ?? heldDefault);

    /// <summary>The path the resolver gives a handle; <c>null</c> for the empty handle, or with no resolver or path.</summary>
    public static TreeNode WriteReference(uint index, uint generation, string target)
    {
        if (generation == 0 || TreeReferences.Resolver is not { } resolver)
            return TreeNode.Null;
        string? path = resolver.PathOf(index, generation, target);
        return string.IsNullOrEmpty(path) ? TreeNode.Null : TreeNode.From(path);
    }

    /// <summary>
    /// <c>null</c> as the empty handle; a path as the handle the resolver gives it, or the empty handle when it gives none
    /// or there is no resolver; the fallback for any other node.
    /// </summary>
    public static THandle ReadReference<THandle>(TreeNode node, string target, THandle fallback, Func<uint, uint, THandle> make)
    {
        if (node.Kind == TreeKind.Null)
            return make(0, 0);
        if (node.Kind != TreeKind.String)
            return fallback;
        if (TreeReferences.Resolver is { } resolver && resolver.TryResolve(node.AsString, target, out uint index, out uint generation))
            return make(index, generation);
        return make(0, 0);
    }

    /// <summary>An array of <paramref name="count"/> numbers, as a bound type is written; null for any other node.</summary>
    public static float[]? ReadFloats(TreeNode node, int count)
    {
        if (node.Kind != TreeKind.Array || node.Count != count)
            return null;
        var components = new float[count];
        for (int i = 0; i < count; ++i)
        {
            TreeNode element = node.Elements[i];
            if (!element.IsNumber)
                return null;
            components[i] = ReadSingle(element, 0);
        }
        return components;
    }

    /// <summary>A field's or element's type in a description entry: its kind, what it names, and its element's type.</summary>
    public static TreeNode Type(string kind, string? target = null, TreeNode? element = null)
    {
        TreeNode type = TreeNode.NewObject().Add("kind", TreeNode.From(kind));
        if (target != null)
            type.Add("target", TreeNode.From(target));
        if (element != null)
            type.Add("element", element);
        return type;
    }

    /// <summary>An array of strings.</summary>
    public static TreeNode Strings(params string[] values)
    {
        TreeNode array = TreeNode.NewArray();
        foreach (string value in values)
            array.Add(TreeNode.From(value));
        return array;
    }
}
