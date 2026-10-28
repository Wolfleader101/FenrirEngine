using Tomlyn;
using Tomlyn.Model;

namespace Iris.Bindings;

/// <summary>
/// The C# type that stands for a bound type, the components its constructor takes, in order, the C# expression of its
/// own default (C++'s <c>T{}</c>) when that is not all zero bytes, for a bound type of one member that a native takes
/// or returns by value, that member's kind (<c>wrapper</c>), which the description does not record, and the header of
/// its Iris mapping (<c>include</c>), which a binding's mappings header includes when it binds the type.
/// </summary>
internal sealed record BoundType(string Type, IReadOnlyList<string>? Constructor, string? Default, string? Wrapper, string? Include)
{
    /// <summary>Whether two entries say the same in every field (the record's equality compares the list by reference).</summary>
    public bool Same(BoundType other) =>
        Type == other.Type && Default == other.Default && Wrapper == other.Wrapper && Include == other.Include &&
        (Constructor == null ? other.Constructor == null : other.Constructor != null && Constructor.SequenceEqual(other.Constructor));
}

/// <summary>
/// How a field holding a mirror is shown (<c>[csharp] project-as</c>): the C# type of the property over it, the member of
/// the backing field its getter reads, and the expression its setter assigns (the projected <c>value</c> itself, through
/// the mirror's conversion, when null).
/// </summary>
internal sealed record Projection(string Type, string Get, string? Set);

/// <summary>
/// Where the C# mirrors go, the names of the SDK types they use (each only when the binding mirrors a type that needs
/// it: <see cref="Binding" />'s accessors say which), the fields (<c>Type::member</c>) mirrored as private, or as fixed
/// buffers rather than <c>[InlineArray]</c> structs, the structs mirrored <c>internal</c>, and the mirrors a field
/// holding one shows as another type (by C++ name).
/// </summary>
internal sealed record CSharpConfig(
    string Contract,
    string Generated,
    IReadOnlyList<string> Usings,
    string? ComponentAttribute,
    string? ComponentInterface,
    string? ReadOnlyComponentInterface,
    string? EventAttribute,
    string? EventInterface,
    string? TaggedUnionAttribute,
    string? ResourceAttribute,
    string? ResourceInterface,
    string? ReadOnlyResourceInterface,
    string PointerType,
    bool ReadOnlyEvents,
    IReadOnlySet<string> ReadOnlyStructs,
    IReadOnlySet<string> ImmutableStructs,
    IReadOnlySet<string> PrivateFields,
    IReadOnlySet<string> FixedBuffers,
    IReadOnlySet<string> InternalStructs,
    IReadOnlyDictionary<string, Projection> Projections);

/// <summary>Where the Iris mappings go, and the headers they include.</summary>
internal sealed record CppConfig(string Mappings, IReadOnlyList<string> Includes);

/// <summary>
/// The registration lists: the source, its function, the extension's type, the maker (<c>maker</c>, optional: the
/// function that makes the extension from its state, which takes the register function's place in the natives header
/// and makes the register function file-local), and the factory each kind of registration is made by (each only when the
/// generated lists hold a type of that kind), the types registered read-only, and those the extension registers by hand.
/// What the source includes comes through the natives header (<c>[natives] includes</c>).
/// </summary>
internal sealed record RegistrationConfig(
    string Source,
    string Function,
    string Extension,
    string? Maker,
    string? MakeComponent,
    string? MakeReadOnlyComponent,
    string? MakeEvent,
    string? MakeSharedStruct,
    string? MakeSceneResource,
    string? MakeReadOnlySceneResource,
    string? MakeAppResource,
    string? MakeReadOnlyAppResource,
    IReadOnlySet<string> ReadOnly,
    IReadOnlySet<string> ByHand);

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
/// The native functions (<c>[natives]</c>): their description (<c>&lt;Name&gt;.natives.json</c> beside the
/// configuration), the natives header (<c>header</c>) holding their declarations and the headers it includes (what
/// the declarations name), the C# class declaring them, the SDK's attribute, the C++ factory each is registered by, the
/// C++ template (<c>zero-outputs</c>) the registration names in place of a native with outputs, as
/// <c>&lt;zero-outputs&gt;&lt;&amp;F, indices...&gt;::Call</c>, which zeroes each before the native runs, and the C#
/// type of each alias a native takes. <c>sources</c> and <c>context</c> are the header reader's.
/// </summary>
internal sealed record NativesConfig(
    string Description,
    string Header,
    IReadOnlyList<string> Includes,
    string Class,
    string Attribute,
    string Make,
    string ZeroOutputs,
    IReadOnlyDictionary<string, string> Types);

/// <summary>
/// A binding's configuration (<c>&lt;Name&gt;.binding.toml</c>, the tool's README): everything about the C# side and
/// the extension that the language-neutral description leaves out. Paths are relative to the file and made absolute
/// here. <c>Descriptions</c> are the descriptions it binds, when it names them; <c>Uses</c> the configurations of the
/// bindings whose mirrors it names rather than generating its own; <c>Defaults</c> the SDK's defaults file
/// (<see cref="Bindings.Defaults" />), whose names are merged in before the rest is read, or null, and
/// <c>DefaultBoundTypes</c> its <c>[bound-types]</c>, kept apart from the binding's own since an inherited entry is
/// consulted only for a type described here.
/// </summary>
internal sealed record BindingConfig(
    string Path,
    string Name,
    string Namespace,
    string RegisteredNamespace,
    IReadOnlyList<string> Descriptions,
    IReadOnlyList<string> Uses,
    string? Defaults,
    IReadOnlyDictionary<string, BoundType> DefaultBoundTypes,
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

    /// <summary>The <c>[tests]</c> keys of the natives test that the zeroing registration (ADR-0045) replaced.</summary>
    public static readonly IReadOnlyList<string> RemovedNativesTestKeys = ["natives-cpp", "natives-csharp", "natives-class"];

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
        reader.Keys(table, "", "namespace", "registered-namespace", "descriptions", "uses", "defaults", "exclude", "csharp",
                    "cpp", "registration", "tests", "natives", "bound-types", "names", "nouns");
        // The SDK's names come in first, so every section below is read as if the binding had spelled them.
        var defaults = table.ContainsKey("defaults") ? reader.PathOf(table, "defaults") : null;
        var defaultBoundTypes = new Dictionary<string, BoundType>(StringComparer.Ordinal);
        if (defaults != null && Bindings.Defaults.Apply(table, defaults, path) is { } defaultBound)
        {
            defaultBoundTypes = ReadBoundTypes(new Reader(defaults), defaultBound, defaults);
        }

        var csharp = reader.Table(table, "csharp");
        reader.Keys(csharp, "csharp", "contract", "generated", "usings", "component-attribute", "component-interface",
                    "read-only-component-interface", "event-attribute", "event-interface", "tagged-union-attribute",
                    "resource-attribute", "resource-interface", "read-only-resource-interface", "pointer-type",
                    "readonly-events", "readonly-structs", "immutable-structs", "private-fields", "fixed-buffers",
                    "internal-structs", "project-as");
        var cpp = reader.Table(table, "cpp");
        reader.Keys(cpp, "cpp", "mappings", "includes");

        RegistrationConfig? registration = null;
        if (table.ContainsKey("registration"))
        {
            var section = reader.Table(table, "registration");
            // The declarations the includes served moved into the natives header, and the key with them: a configuration
            // still stating it is told where, rather than that the key is unknown.
            if (section.ContainsKey("includes"))
            {
                throw new BindingException(
                    $"{path}: [registration] includes moved to [natives] includes, which the natives header (the declarations " +
                    "the includes served) includes");
            }

            reader.Keys(section, "registration", "source", "function", "extension", "maker", "make-component",
                        "make-read-only-component", "make-event", "make-shared-struct", "make-scene-resource",
                        "make-read-only-scene-resource", "make-app-resource", "make-read-only-app-resource", "read-only",
                        "by-hand");
            registration = new RegistrationConfig(
                reader.PathOf(section, "source"), reader.String(section, "function"),
                reader.String(section, "extension"), reader.OptionalString(section, "maker"),
                reader.OptionalString(section, "make-component"),
                reader.OptionalString(section, "make-read-only-component"), reader.OptionalString(section, "make-event"),
                reader.OptionalString(section, "make-shared-struct"), reader.OptionalString(section, "make-scene-resource"),
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
            // The natives test went when the registration took over zeroing outputs: a configuration still naming it
            // is told so, rather than that the keys are unknown.
            var removed = RemovedNativesTestKeys.Where(section.ContainsKey).ToList();
            if (removed.Count > 0)
            {
                throw new BindingException(
                    $"{path}: [tests] {string.Join(", ", removed)}: the natives test is no longer generated, since the " +
                    "registration zeroes each native's outputs ([natives] zero-outputs); remove the key" +
                    (removed.Count > 1 ? "s" : ""));
            }

            reader.Keys(section, "tests", "cpp", "csharp", "class", "includes", "setup", "component", "tags");
            tests = new TestsConfig(reader.PathOf(section, "cpp"), reader.PathOf(section, "csharp"),
                                    reader.String(section, "class"), reader.Strings(section, "includes"),
                                    reader.String(section, "setup"), reader.String(section, "component"),
                                    reader.String(section, "tags"));
        }

        NativesConfig? nativesConfig = null;
        if (table.ContainsKey("natives"))
        {
            var section = reader.Table(table, "natives");
            reader.Keys(section, "natives", "sources", "context", "header", "includes", "class", "attribute", "make",
                        "zero-outputs", "types");
            nativesConfig = new NativesConfig(
                System.IO.Path.Combine(System.IO.Path.GetDirectoryName(path)!, fileName[..^Extension.Length] + ".natives.json"),
                reader.PathOf(section, "header"), reader.Strings(section, "includes"),
                reader.String(section, "class"), reader.String(section, "attribute"), reader.String(section, "make"),
                reader.String(section, "zero-outputs"), reader.StringTable(section, "types"));
            if (registration is null)
            {
                throw new BindingException($"{path}: [natives] needs [registration], whose source registers them");
            }
        }
        else if (registration?.Maker != null)
        {
            throw new BindingException(
                $"{path}: [registration] maker needs [natives]: the maker takes the natives' extension state, which their description records");
        }

        var boundTypes = table.ContainsKey("bound-types")
                             ? ReadBoundTypes(reader, reader.Table(table, "bound-types"), path)
                             : new Dictionary<string, BoundType>(StringComparer.Ordinal);
        foreach (var (name, bound) in boundTypes.Where(entry => defaultBoundTypes.TryGetValue(entry.Key, out var given) && given.Same(entry.Value)))
        {
            throw new BindingException($"{path}: [bound-types] \"{name}\" is already the default from {defaults}; delete it");
        }

        var space = reader.String(table, "namespace");
        return new BindingConfig(
            path, fileName[..^Extension.Length], space,
            table.ContainsKey("registered-namespace") ? reader.String(table, "registered-namespace") : space,
            reader.Paths(table, "descriptions"), reader.Paths(table, "uses"), defaults, defaultBoundTypes,
            new CSharpConfig(reader.PathOf(csharp, "contract"), reader.PathOf(csharp, "generated"),
                             reader.Strings(csharp, "usings"), reader.OptionalString(csharp, "component-attribute"),
                             reader.OptionalString(csharp, "component-interface"),
                             reader.OptionalString(csharp, "read-only-component-interface"),
                             reader.OptionalString(csharp, "event-attribute"), reader.OptionalString(csharp, "event-interface"),
                             reader.OptionalString(csharp, "tagged-union-attribute"),
                             reader.OptionalString(csharp, "resource-attribute"),
                             reader.OptionalString(csharp, "resource-interface"),
                             reader.OptionalString(csharp, "read-only-resource-interface"),
                             csharp.ContainsKey("pointer-type") ? reader.String(csharp, "pointer-type") : "ulong",
                             csharp.ContainsKey("readonly-events") && reader.Boolean(csharp, "readonly-events"),
                             reader.Strings(csharp, "readonly-structs").ToHashSet(StringComparer.Ordinal),
                             reader.Strings(csharp, "immutable-structs").ToHashSet(StringComparer.Ordinal),
                             reader.Strings(csharp, "private-fields").ToHashSet(StringComparer.Ordinal),
                             reader.Strings(csharp, "fixed-buffers").ToHashSet(StringComparer.Ordinal),
                             reader.Strings(csharp, "internal-structs").ToHashSet(StringComparer.Ordinal),
                             ReadProjections(reader, csharp, path)),
            new CppConfig(reader.PathOf(cpp, "mappings"), reader.Strings(cpp, "includes")), registration, tests,
            nativesConfig, boundTypes, reader.StringTable(table, "names"), reader.StringTable(table, "nouns"),
            reader.Strings(table, "exclude").ToHashSet(StringComparer.Ordinal));
    }

    // A [bound-types] table, the binding's own or its defaults file's.
    private static Dictionary<string, BoundType> ReadBoundTypes(Reader reader, TomlTable table, string path)
    {
        var boundTypes = new Dictionary<string, BoundType>(StringComparer.Ordinal);
        foreach (var (name, value) in table)
        {
            if (value is not TomlTable bound)
            {
                throw new BindingException(
                    $"{path}: bound type '{name}' is a table: {{ type = \"<C# type>\", constructor = [...] }}");
            }

            reader.Keys(bound, $"bound-types.\"{name}\"", "type", "constructor", "default", "wrapper", "include");
            boundTypes.Add(name, new BoundType(reader.String(bound, "type"),
                                               bound.ContainsKey("constructor") ? reader.Strings(bound, "constructor") : null,
                                               reader.OptionalString(bound, "default"), reader.OptionalString(bound, "wrapper"),
                                               reader.OptionalString(bound, "include")));
        }

        return boundTypes;
    }

    // The [csharp] project-as table: each described type name to how a field holding its mirror is shown.
    private static Dictionary<string, Projection> ReadProjections(Reader reader, TomlTable csharp, string path)
    {
        var projections = new Dictionary<string, Projection>(StringComparer.Ordinal);
        if (!csharp.ContainsKey("project-as"))
        {
            return projections;
        }

        foreach (var (name, value) in reader.Table(csharp, "project-as"))
        {
            if (value is not TomlTable projection)
            {
                throw new BindingException(
                    $"{path}: project-as '{name}' is a table: {{ type = \"<C# type>\", get = \"<member>\", set = \"<expression>\" }}");
            }

            reader.Keys(projection, $"csharp.project-as.\"{name}\"", "type", "get", "set");
            projections.Add(name, new Projection(reader.String(projection, "type"), reader.String(projection, "get"),
                                                 reader.OptionalString(projection, "set")));
        }

        return projections;
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
