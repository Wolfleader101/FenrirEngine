using Tomlyn;
using Tomlyn.Model;

namespace Iris.Bindings;

/// <summary>
/// A binding's defaults file (<c>&lt;Name&gt;.binding-defaults.toml</c>, named by the configuration's <c>defaults</c>): the
/// SDK's names, stated once for every binding of that SDK, in the configuration's own sections. It may hold only the keys
/// that name an SDK thing (<see cref="Allowed" />), never a path, a role list or a name of one binding's. A scalar the
/// binding states replaces the default; a list the binding states comes after the default's entries; a binding restating
/// a default byte for byte is refused, so a configuration cannot drift back to spelling what the file already says.
/// </summary>
internal static class Defaults
{
    public const string Extension = ".binding-defaults.toml";

    /// <summary>The keys a defaults file may state, by section.</summary>
    public static readonly IReadOnlyDictionary<string, IReadOnlyList<string>> Allowed = new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal)
    {
        ["csharp"] =
        [
            "component-attribute", "component-interface", "read-only-component-interface", "event-attribute", "event-interface",
            "tagged-union-attribute", "resource-attribute", "resource-interface", "read-only-resource-interface", "pointer-type",
            "usings",
        ],
        ["cpp"] = ["includes"],
        ["registration"] =
        [
            "extension", "make-component", "make-read-only-component", "make-event", "make-shared-struct", "make-scene-resource",
            "make-read-only-scene-resource", "make-app-resource", "make-read-only-app-resource",
        ],
        ["tests"] = ["includes", "component"],
        ["natives"] = ["context", "attribute", "make", "zero-outputs", "includes"],
        // Any entry: an inherited bound type is consulted only for a type described here, so the table is handed back
        // rather than merged, and its entries are checked where the binding's own are.
        ["bound-types"] = [],
    };

    /// <summary>
    /// Reads the defaults file and merges it into the binding's table, section by section, for the sections the binding
    /// has (a section it lacks is not added: a binding with no <c>[registration]</c> gets none). Returns the file's
    /// <c>[bound-types]</c>, or null when it has none. Throws <see cref="BindingException" /> for a file that is missing,
    /// misnamed, states a key a defaults file may not, or one the binding restates.
    /// </summary>
    public static TomlTable? Apply(TomlTable binding, string defaultsPath, string bindingPath)
    {
        if (!defaultsPath.EndsWith(Extension, StringComparison.Ordinal) || Path.GetFileName(defaultsPath).Length == Extension.Length)
        {
            throw new BindingException($"{bindingPath}: defaults names {defaultsPath}; a defaults file is named <Name>{Extension}");
        }

        if (!File.Exists(defaultsPath))
        {
            throw new BindingException($"{bindingPath}: defaults names {defaultsPath}, which does not exist");
        }

        TomlTable defaults;
        try
        {
            defaults = Toml.ToModel(File.ReadAllText(defaultsPath), defaultsPath);
        }
        catch (TomlException exception)
        {
            throw new BindingException($"{defaultsPath}: {exception.Message}");
        }

        TomlTable? boundTypes = null;
        foreach (var (name, value) in defaults)
        {
            if (!Allowed.TryGetValue(name, out var allowed) || value is not TomlTable section)
            {
                throw new BindingException(
                    $"{defaultsPath}: '{name}' is not a section a defaults file may state (one of [{string.Join("], [", Allowed.Keys)}])");
            }

            if (name == "bound-types")
            {
                boundTypes = section;
                continue;
            }

            foreach (var key in section.Keys.Where(key => !allowed.Contains(key)))
            {
                throw new BindingException(
                    $"{defaultsPath}: '{key}' in [{name}] is not a key a defaults file may state (a binding's own); it may state " +
                    string.Join(", ", allowed));
            }

            if (binding.TryGetValue(name, out var stated) && stated is TomlTable own)
            {
                Merge(own, section, name, bindingPath, defaultsPath);
            }
        }

        return boundTypes;
    }

    private static void Merge(TomlTable own, TomlTable defaults, string section, string bindingPath, string defaultsPath)
    {
        foreach (var (key, value) in defaults)
        {
            if (!own.TryGetValue(key, out var stated))
            {
                own[key] = value;
                continue;
            }

            if (value is TomlArray list && stated is TomlArray ownList)
            {
                foreach (var item in ownList.Where(item => list.Any(entry => Equals(entry, item))))
                {
                    throw new BindingException(
                        $"{bindingPath}: [{section}] {key} lists {Spell(item)}, which is already the default from {defaultsPath}; delete it");
                }

                var merged = new TomlArray();
                foreach (var item in list.Concat(ownList))
                {
                    merged.Add(item);
                }

                own[key] = merged;
                continue;
            }

            if (Equals(value, stated))
            {
                throw new BindingException(
                    $"{bindingPath}: [{section}] '{key}' is already the default from {defaultsPath}; delete it");
            }
        }
    }

    private static string Spell(object? item) => item is string text ? $"\"{text}\"" : item?.ToString() ?? "null";
}
