using Tomlyn;
using Tomlyn.Model;

namespace Iris.Bindings;

/// <summary>
/// The C# type that stands for a bound type, the components its constructor takes, in order, the C# expression of its
/// own default (C++'s <c>T{}</c>) when that is not all zero bytes, and, for a bound type of one member that a native takes
/// or returns by value, that member's kind (<c>wrapper</c>), which the description does not record.
/// </summary>
internal sealed record BoundType(string Type, IReadOnlyList<string>? Constructor, string? Default, string? Wrapper);

/// <summary>
/// Where the C# mirrors go, the names of the SDK types they use (a resource's only when the binding has one), the
/// fields (<c>Type::member</c>) mirrored as private, or as fixed buffers rather than <c>[InlineArray]</c> structs, and the
/// structs mirrored <c>internal</c>.
/// </summary>
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
    string? ResourceAttribute,
    string? ResourceInterface,
    string? ReadOnlyResourceInterface,
    string PointerType,
    bool ReadOnlyEvents,
    IReadOnlySet<string> ReadOnlyStructs,
    IReadOnlySet<string> PrivateFields,
    IReadOnlySet<string> FixedBuffers,
    IReadOnlySet<string> InternalStructs);

/// <summary>Where the Iris mappings go, and the headers they include.</summary>
internal sealed record CppConfig(string Mappings, IReadOnlyList<string> Includes);

/// <summary>
/// The registration lists: the source, its function, and the factory each kind of registration is made by (a resource's
/// only when the binding has one), the types registered read-only, and those the extension registers by hand.
/// </summary>
internal sealed record RegistrationConfig(
    string Source,
    IReadOnlyList<string> Includes,
    string Function,
    string Extension,
    string MakeComponent,
    string MakeReadOnlyComponent,
    string MakeEvent,
    string MakeSharedStruct,
    string? MakeSceneResource,
    string? MakeReadOnlySceneResource,
    string? MakeAppResource,
    string? MakeReadOnlyAppResource,
    IReadOnlySet<string> ReadOnly,
    IReadOnlySet<string> ByHand)
{
    /// <summary>Whether it names a resource factory, so the generated function sets the extension's resources.</summary>
    public bool HasResources => MakeSceneResource != null || MakeReadOnlySceneResource != null || MakeAppResource != null ||
                                MakeReadOnlyAppResource != null;
}

/// <summary>
/// The defaults test: its two sources, the C# class, and how a test case reaches the loaded C# assembly; and with
/// natives, the natives test's two sources and C# class.
/// </summary>
internal sealed record TestsConfig(
    string Cpp,
    string CSharp,
    string Class,
    IReadOnlyList<string> Includes,
    string Setup,
    string Component,
    string Tags,
    string? NativesCpp,
    string? NativesCSharp,
    string? NativesClass);

/// <summary>
/// The native functions (<c>[natives]</c>): their description (<c>&lt;Name&gt;.natives.json</c> beside the
/// configuration), the C# class declaring them, the SDK's attribute, the C++ factory each is registered by, and the C#
/// type of each alias a native takes. <c>sources</c> and <c>context</c> are the header reader's.
/// </summary>
internal sealed record NativesConfig(
    string Description,
    string Context,
    string Class,
    string Attribute,
    string Make,
    IReadOnlyDictionary<string, string> Types);

/// <summary>
/// A binding's configuration (<c>&lt;Name&gt;.binding.toml</c>, the tool's README): everything about the C# side and
/// the extension that the language-neutral description leaves out. Paths are relative to the file and made absolute
/// here. <c>Descriptions</c> are the descriptions it binds, when it names them; <c>Uses</c> the configurations of the
/// bindings whose mirrors it names rather than generating its own.
/// </summary>
internal sealed record BindingConfig(
    string Path,
    string Name,
    string Namespace,
    string RegisteredNamespace,
    IReadOnlyList<string> Descriptions,
    IReadOnlyList<string> Uses,
    CSharpConfig CSharp,
    CppConfig Cpp,
    RegistrationConfig? Registration,
    TestsConfig? Tests,
    NativesConfig? Natives,
    IReadOnlyDictionary<string, BoundType> BoundTypes,
    IReadOnlyDictionary<string, string> Names,
    IReadOnlyDictionary<string, string> Nouns,
    IReadOnlySet<string> Exclude)
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
        reader.Keys(table, "", "namespace", "registered-namespace", "descriptions", "uses", "exclude", "csharp", "cpp",
                    "registration", "tests", "natives", "bound-types", "names", "nouns");
        var csharp = reader.Table(table, "csharp");
        reader.Keys(csharp, "csharp", "contract", "generated", "usings", "component-attribute", "component-interface",
                    "read-only-component-interface", "event-attribute", "event-interface", "tagged-union-attribute",
                    "resource-attribute", "resource-interface", "read-only-resource-interface", "pointer-type",
                    "readonly-events", "readonly-structs", "private-fields", "fixed-buffers", "internal-structs");
        var cpp = reader.Table(table, "cpp");
        reader.Keys(cpp, "cpp", "mappings", "includes");

        RegistrationConfig? registration = null;
        if (table.ContainsKey("registration"))
        {
            var section = reader.Table(table, "registration");
            reader.Keys(section, "registration", "source", "includes", "function", "extension", "make-component",
                        "make-read-only-component", "make-event", "make-shared-struct", "make-scene-resource",
                        "make-read-only-scene-resource", "make-app-resource", "make-read-only-app-resource", "read-only",
                        "by-hand");
            registration = new RegistrationConfig(
                reader.PathOf(section, "source"), reader.Strings(section, "includes"), reader.String(section, "function"),
                reader.String(section, "extension"), reader.String(section, "make-component"),
                reader.String(section, "make-read-only-component"), reader.String(section, "make-event"),
                reader.String(section, "make-shared-struct"), reader.OptionalString(section, "make-scene-resource"),
                reader.OptionalString(section, "make-read-only-scene-resource"),
                reader.OptionalString(section, "make-app-resource"),
                reader.OptionalString(section, "make-read-only-app-resource"),
                reader.Strings(section, "read-only").ToHashSet(StringComparer.Ordinal),
                reader.Strings(section, "by-hand").ToHashSet(StringComparer.Ordinal));
        }

        TestsConfig? tests = null;
        if (table.ContainsKey("tests"))
        {
            var section = reader.Table(table, "tests");
            reader.Keys(section, "tests", "cpp", "csharp", "class", "includes", "setup", "component", "tags",
                        "natives-cpp", "natives-csharp", "natives-class");
            var natives = section.ContainsKey("natives-cpp") || section.ContainsKey("natives-csharp") ||
                          section.ContainsKey("natives-class");
            tests = new TestsConfig(reader.PathOf(section, "cpp"), reader.PathOf(section, "csharp"),
                                    reader.String(section, "class"), reader.Strings(section, "includes"),
                                    reader.String(section, "setup"), reader.String(section, "component"),
                                    reader.String(section, "tags"),
                                    natives ? reader.PathOf(section, "natives-cpp") : null,
                                    natives ? reader.PathOf(section, "natives-csharp") : null,
                                    natives ? reader.String(section, "natives-class") : null);
        }

        NativesConfig? nativesConfig = null;
        if (table.ContainsKey("natives"))
        {
            var section = reader.Table(table, "natives");
            reader.Keys(section, "natives", "sources", "context", "class", "attribute", "make", "types");
            nativesConfig = new NativesConfig(
                System.IO.Path.Combine(System.IO.Path.GetDirectoryName(path)!, fileName[..^Extension.Length] + ".natives.json"),
                reader.String(section, "context"), reader.String(section, "class"), reader.String(section, "attribute"), reader.String(section, "make"),
                reader.StringTable(section, "types"));
            if (registration is null)
            {
                throw new BindingException($"{path}: [natives] needs [registration], whose source registers them");
            }
        }

        if (tests?.NativesCpp is not null && nativesConfig is null)
        {
            throw new BindingException($"{path}: [tests] natives-cpp needs [natives]");
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

                reader.Keys(bound, $"bound-types.\"{name}\"", "type", "constructor", "default", "wrapper");
                boundTypes.Add(name, new BoundType(reader.String(bound, "type"),
                                                   bound.ContainsKey("constructor") ? reader.Strings(bound, "constructor") : null,
                                                   reader.OptionalString(bound, "default"),
                                                   reader.OptionalString(bound, "wrapper")));
            }
        }

        var space = reader.String(table, "namespace");
        return new BindingConfig(
            path, fileName[..^Extension.Length], space,
            table.ContainsKey("registered-namespace") ? reader.String(table, "registered-namespace") : space,
            reader.Paths(table, "descriptions"), reader.Paths(table, "uses"),
            new CSharpConfig(reader.PathOf(csharp, "contract"), reader.PathOf(csharp, "generated"),
                             reader.Strings(csharp, "usings"), reader.String(csharp, "component-attribute"),
                             reader.String(csharp, "component-interface"),
                             reader.String(csharp, "read-only-component-interface"),
                             reader.String(csharp, "event-attribute"), reader.String(csharp, "event-interface"),
                             reader.String(csharp, "tagged-union-attribute"),
                             reader.OptionalString(csharp, "resource-attribute"),
                             reader.OptionalString(csharp, "resource-interface"),
                             reader.OptionalString(csharp, "read-only-resource-interface"),
                             csharp.ContainsKey("pointer-type") ? reader.String(csharp, "pointer-type") : "ulong",
                             csharp.ContainsKey("readonly-events") && reader.Boolean(csharp, "readonly-events"),
                             reader.Strings(csharp, "readonly-structs").ToHashSet(StringComparer.Ordinal),
                             reader.Strings(csharp, "private-fields").ToHashSet(StringComparer.Ordinal),
                             reader.Strings(csharp, "fixed-buffers").ToHashSet(StringComparer.Ordinal),
                             reader.Strings(csharp, "internal-structs").ToHashSet(StringComparer.Ordinal)),
            new CppConfig(reader.PathOf(cpp, "mappings"), reader.Strings(cpp, "includes")), registration, tests,
            nativesConfig, boundTypes, reader.StringTable(table, "names"), reader.StringTable(table, "nouns"),
            reader.Strings(table, "exclude").ToHashSet(StringComparer.Ordinal));
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

        public string? OptionalString(TomlTable table, string key) => table.ContainsKey(key) ? String(table, key) : null;

        public IReadOnlyList<string> Paths(TomlTable table, string key) =>
            Strings(table, key).Select(item => System.IO.Path.GetFullPath(System.IO.Path.Combine(directory, item))).ToList();

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
