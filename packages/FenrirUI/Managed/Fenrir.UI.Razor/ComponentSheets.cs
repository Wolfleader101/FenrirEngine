using System.Reflection;
using System.Text;

namespace Fenrir.UI.Razor;

/// <summary>
/// Razor component stylesheets scoped by a scope class (ADR-0070): the class every element a component's builder makes
/// carries, and what FenrirUI's stylesheet loader appends to each rule's rightmost compound.
/// </summary>
internal static class ComponentScope
{
    private const uint OffsetBasis = 2166136261;
    private const uint Prime = 16777619;

    /// <summary>The scope classes found, by type, until <see cref="Release"/>.</summary>
    private static readonly Dictionary<Type, string> classes = [];

    /// <summary>
    /// The scope class of a Razor component type: <c>s-</c> followed by eight lowercase hex digits of the 32-bit
    /// FNV-1a hash of its full name's UTF-8 bytes. A generic type's is its definition's, so every instantiation
    /// shares the one sheet its file has.
    /// </summary>
    public static string ClassOf(Type type)
    {
        if (!classes.TryGetValue(type, out string? scope))
        {
            Type named = type.IsConstructedGenericType ? type.GetGenericTypeDefinition() : type;
            scope = ClassOf(named.FullName ?? named.Name);
            classes.Add(type, scope);
        }
        return scope;
    }

    /// <summary>The scope class of a Razor component type with this full name.</summary>
    public static string ClassOf(string fullName)
    {
        uint hash = OffsetBasis;
        foreach (byte b in Encoding.UTF8.GetBytes(fullName))
        {
            hash ^= b;
            hash *= Prime;
        }
        return $"s-{hash:x8}";
    }

    /// <summary>Forgets the scope classes found, which name a script generation's types.</summary>
    internal static void Release() => classes.Clear();
}

/// <summary>
/// The Razor component sheets one UI root has loaded (ADR-0070): each type's <c>[ComponentStylesheet]</c> once, on its
/// first mount there, and each <c>&lt;style&gt;</c> block of each type once. It goes with its root's group.
/// </summary>
internal sealed class ComponentSheets
{
    private readonly HashSet<Type> types = [];
    private readonly HashSet<(Type Type, int Sequence, int Ordinal)> blocks = [];

    /// <summary>
    /// Loads the type's own sheet with its scope, the first time the type is seen here: by path when the file is
    /// there, so hot reload applies, otherwise from its text. A type with none loads nothing.
    /// </summary>
    public void Load(Type type, IElementSeam seam)
    {
        if (!types.Add(type))
            return;
        ComponentStylesheetAttribute? sheet = type.GetCustomAttribute<ComponentStylesheetAttribute>(inherit: false);
        if (sheet == null)
            return;
        string scope = ComponentScope.ClassOf(type);
        if (sheet.Path.Length != 0 && File.Exists(sheet.Path) && seam.LoadStylesheet(sheet.Path, scope))
            return;
        seam.AddStylesheet(sheet.Text, sheet.Path.Length != 0 ? sheet.Path : $"{type.FullName}.razor.css", scope);
    }

    /// <summary>
    /// Adds a <c>&lt;style&gt;</c> block of the type with its scope, the first time the block is seen here, named by
    /// the type and the block's sequence (and its ordinal, for a folded string's second block and later). A block is
    /// read once: text that changes in a later build (a block with C# in it) is not read again.
    /// </summary>
    public void AddBlock(Type type, int sequence, int ordinal, string css, IElementSeam seam)
    {
        if (!blocks.Add((type, sequence, ordinal)))
            return;
        string name = $"{type.FullName}.razor <style> at sequence {sequence}" + (ordinal == 0 ? "" : $" ({ordinal + 1})");
        seam.AddStylesheet(css, name, ComponentScope.ClassOf(type));
    }
}
