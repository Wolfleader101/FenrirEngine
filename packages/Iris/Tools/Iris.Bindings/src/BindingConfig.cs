using Tomlyn;
using Tomlyn.Model;

namespace Iris.Bindings;

/// <summary>The C# type that stands for a bound type, and the components its constructor takes, in order.</summary>
internal sealed record BoundType(string Type, IReadOnlyList<string>? Constructor);

/// <summary>Where the C# mirrors go, and the names of the SDK types they use.</summary>
internal sealed record CSharpConfig(
    string Contract,
    string Generated,
    IReadOnlyList<string> Usings,
    string ComponentAttribute,
    string ComponentInterface,
    string ReadOnlyComponentInterface,
    string EventAttribute,
    string EventInterface,
    string TaggedUnionAttribute,
    string PointerType,
    bool ReadOnlyEvents);

/// <summary>Where the Iris mappings go, and the headers they include.</summary>
internal sealed record CppConfig(string Mappings, IReadOnlyList<string> Includes);

/// <summary>The registration lists: the source, its function, and the factory each kind of registration is made by.</summary>
internal sealed record RegistrationConfig(
    string Source,
    IReadOnlyList<string> Includes,
    string Function,
    string Extension,
    string MakeComponent,
    string MakeReadOnlyComponent,
    string MakeEvent,
    string MakeSharedStruct,
    IReadOnlySet<string> ReadOnly);

/// <summary>The defaults test: its two sources, the C# class, and how a test case reaches the loaded C# assembly.</summary>
internal sealed record TestsConfig(
    string Cpp,
    string CSharp,
    string Class,
    IReadOnlyList<string> Includes,
    string Setup,
    string Component,
    string Tags);

/// <summary>
/// A binding's configuration (<c>&lt;Name&gt;.binding.toml</c>, the tool's README): everything about the C# side and
/// the extension that the language-neutral description leaves out. Paths are relative to the file and made absolute
/// here.
/// </summary>
internal sealed record BindingConfig(
    string Path,
    string Name,
    string Namespace,
    CSharpConfig CSharp,
    CppConfig Cpp,
    RegistrationConfig? Registration,
    TestsConfig? Tests,
    IReadOnlyDictionary<string, BoundType> BoundTypes,
    IReadOnlyDictionary<string, string> Names,
    IReadOnlyDictionary<string, string> Nouns)
{
    public const string Extension = ".binding.toml";

    /// <summary>Reads a configuration file, or throws <see cref="BindingException" /> saying what is wrong with it.</summary>
    public static BindingConfig Read(string path)
    {
        path = System.IO.Path.GetFullPath(path);
        var fileName = System.IO.Path.GetFileName(path);
        if (!fileName.EndsWith(Extension, StringComparison.Ordinal) || fileName.Length == Extension.Length)
        {
            throw new BindingException($"{path}: a binding's configuration is named <Name>{Extension}");
        }

        TomlTable table;
        try
        {
            table = Toml.ToModel(File.ReadAllText(path), path);
        }
        catch (TomlException exception)
        {
            throw new BindingException($"{path}: {exception.Message}");
        }

        var reader = new Reader(path);
        reader.Keys(table, "", "namespace", "csharp", "cpp", "registration", "tests", "bound-types", "names", "nouns");
        var csharp = reader.Table(table, "csharp");
        reader.Keys(csharp, "csharp", "contract", "generated", "usings", "component-attribute", "component-interface",
                    "read-only-component-interface", "event-attribute", "event-interface", "tagged-union-attribute",
                    "pointer-type", "readonly-events");
        var cpp = reader.Table(table, "cpp");
        reader.Keys(cpp, "cpp", "mappings", "includes");

        RegistrationConfig? registration = null;
        if (table.ContainsKey("registration"))
        {
            var section = reader.Table(table, "registration");
            reader.Keys(section, "registration", "source", "includes", "function", "extension", "make-component",
                        "make-read-only-component", "make-event", "make-shared-struct", "read-only");
            registration = new RegistrationConfig(
                reader.PathOf(section, "source"), reader.Strings(section, "includes"), reader.String(section, "function"),
                reader.String(section, "extension"), reader.String(section, "make-component"),
                reader.String(section, "make-read-only-component"), reader.String(section, "make-event"),
                reader.String(section, "make-shared-struct"),
                reader.Strings(section, "read-only").ToHashSet(StringComparer.Ordinal));
        }

        TestsConfig? tests = null;
        if (table.ContainsKey("tests"))
        {
            var section = reader.Table(table, "tests");
            reader.Keys(section, "tests", "cpp", "csharp", "class", "includes", "setup", "component", "tags");
            tests = new TestsConfig(reader.PathOf(section, "cpp"), reader.PathOf(section, "csharp"),
                                    reader.String(section, "class"), reader.Strings(section, "includes"),
                                    reader.String(section, "setup"), reader.String(section, "component"),
                                    reader.String(section, "tags"));
        }

        var boundTypes = new Dictionary<string, BoundType>(StringComparer.Ordinal);
        if (table.ContainsKey("bound-types"))
        {
            foreach (var (name, value) in reader.Table(table, "bound-types"))
            {
                if (value is not TomlTable bound)
                {
                    throw new BindingException(
                        $"{path}: bound type '{name}' is a table: {{ type = \"<C# type>\", constructor = [...] }}");
                }

                reader.Keys(bound, $"bound-types.\"{name}\"", "type", "constructor");
                boundTypes.Add(name, new BoundType(reader.String(bound, "type"),
                                                   bound.ContainsKey("constructor") ? reader.Strings(bound, "constructor") : null));
            }
        }

        return new BindingConfig(
            path, fileName[..^Extension.Length], reader.String(table, "namespace"),
            new CSharpConfig(reader.PathOf(csharp, "contract"), reader.PathOf(csharp, "generated"),
                             reader.Strings(csharp, "usings"), reader.String(csharp, "component-attribute"),
                             reader.String(csharp, "component-interface"),
                             reader.String(csharp, "read-only-component-interface"),
                             reader.String(csharp, "event-attribute"), reader.String(csharp, "event-interface"),
                             reader.String(csharp, "tagged-union-attribute"),
                             csharp.ContainsKey("pointer-type") ? reader.String(csharp, "pointer-type") : "ulong",
                             csharp.ContainsKey("readonly-events") && reader.Boolean(csharp, "readonly-events")),
            new CppConfig(reader.PathOf(cpp, "mappings"), reader.Strings(cpp, "includes")), registration, tests,
            boundTypes, reader.StringTable(table, "names"), reader.StringTable(table, "nouns"));
    }

    private sealed class Reader(string path)
    {
        private readonly string directory = System.IO.Path.GetDirectoryName(path)!;

        public void Keys(TomlTable table, string section, params string[] known)
        {
            foreach (var key in table.Keys.Where(key => !known.Contains(key)))
            {
                var where = section.Length == 0 ? "" : $" in [{section}]";
                throw new BindingException($"{path}: unknown key '{key}'{where} (known: {string.Join(", ", known)})");
            }
        }

        public TomlTable Table(TomlTable table, string key) =>
            table.TryGetValue(key, out var value) && value is TomlTable section
                ? section
                : throw new BindingException($"{path}: [{key}] is missing");

        public string String(TomlTable table, string key) =>
            table.TryGetValue(key, out var value) && value is string text && text.Length > 0
                ? text
                : throw new BindingException($"{path}: '{key}' is missing or is not a string");

        public bool Boolean(TomlTable table, string key) =>
            table[key] is bool value ? value : throw new BindingException($"{path}: '{key}' is not true or false");

        public string PathOf(TomlTable table, string key) =>
            System.IO.Path.GetFullPath(System.IO.Path.Combine(directory, String(table, key)));

        public IReadOnlyList<string> Strings(TomlTable table, string key)
        {
            if (!table.TryGetValue(key, out var value))
            {
                return [];
            }

            return value is TomlArray array && array.All(item => item is string)
                       ? array.Cast<string>().ToList()
                       : throw new BindingException($"{path}: '{key}' is a list of strings");
        }

        public IReadOnlyDictionary<string, string> StringTable(TomlTable table, string key)
        {
            var result = new Dictionary<string, string>(StringComparer.Ordinal);
            if (!table.ContainsKey(key))
            {
                return result;
            }

            foreach (var (name, value) in Table(table, key))
            {
                result.Add(name, value as string ?? throw new BindingException($"{path}: [{key}] '{name}' is not a string"));
            }

            return result;
        }
    }
}
