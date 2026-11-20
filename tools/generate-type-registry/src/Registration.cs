using System.Text.RegularExpressions;
using Tomlyn;
using Tomlyn.Model;

namespace GenerateTypeRegistry;

/// <summary>
/// A package's <c>[registration]</c> table, read from its selection (<c>&lt;Package&gt;.api.toml</c>): where the generated
/// header and source go, the registration function's qualified name, the headers declaring the described types, and the
/// selections of the packages whose registrations it uses. The paths are relative to the selection's own directory; the
/// description read is <c>&lt;Package&gt;.api.json</c> beside it.
/// </summary>
internal sealed partial record Registration(string SelectionPath, string Package, string HeaderPath, string SourcePath,
                                            string FunctionNamespace, string FunctionName, IReadOnlyList<string> Includes,
                                            IReadOnlyList<string> Uses, string? TestPath)
{
    public const string Extension = ".api.toml";

    /// <summary>The package's description, <c>&lt;Package&gt;.api.json</c> beside the selection.</summary>
    public string DescriptionPath => Path.Combine(Path.GetDirectoryName(SelectionPath)!, DescriptionName);

    /// <summary>The description's file name, which the generated files name as what they were generated from.</summary>
    public string DescriptionName => Package + ".api.json";

    /// <summary>The registration function's qualified name, as a caller outside its namespace spells it.</summary>
    public string Function => FunctionNamespace.Length > 0 ? $"{FunctionNamespace}::{FunctionName}" : FunctionName;

    /// <summary>
    /// The namespace the generated header declares each description in, so another package's registration that uses
    /// this one points its fields at them: the function's namespace, then <c>&lt;Package&gt;Types</c>.
    /// </summary>
    public string DescriptionNamespace =>
        FunctionNamespace.Length > 0 ? $"{FunctionNamespace}::{Package}Types" : $"{Package}Types";

    /// <summary>
    /// The generated header as another package's header includes it: its path below its last <c>include</c>
    /// directory. Throws <see cref="GenerateException" /> when it lies under none, which a used registration's must.
    /// </summary>
    public string HeaderInclude
    {
        get
        {
            var segments = HeaderPath.Replace('\\', '/').Split('/');
            var include = Array.LastIndexOf(segments, "include");
            if (include < 0 || include == segments.Length - 1)
            {
                throw new GenerateException(
                    $"{SelectionPath}: its generated header {HeaderPath} lies under no include directory, so a " +
                    "registration using it cannot include it");
            }

            return string.Join('/', segments[(include + 1)..]);
        }
    }

    /// <summary>
    /// Reads the <c>[registration]</c> table of a selection: null when it has none, so the package registers nothing.
    /// Throws <see cref="GenerateException" /> saying what is wrong with a malformed one.
    /// </summary>
    public static Registration? Read(string selectionPath)
    {
        selectionPath = Path.GetFullPath(selectionPath);
        var fileName = Path.GetFileName(selectionPath);
        if (!fileName.EndsWith(Extension, StringComparison.Ordinal) || fileName.Length == Extension.Length)
        {
            throw new GenerateException($"{selectionPath}: a selection is named <Package>{Extension}");
        }

        TomlTable selection;
        try
        {
            selection = Toml.ToModel(File.ReadAllText(selectionPath), selectionPath);
        }
        catch (TomlException exception)
        {
            throw new GenerateException($"{selectionPath}: {exception.Message}");
        }

        if (!selection.TryGetValue("registration", out var value))
        {
            return null;
        }

        if (value is not TomlTable table)
        {
            throw new GenerateException($"{selectionPath}: 'registration' is a table");
        }

        if (table.Keys.FirstOrDefault(key => key is not ("header" or "source" or "function" or "includes" or "uses" or "test")) is { } key)
        {
            throw new GenerateException(
                $"{selectionPath}: [registration] has an unknown key '{key}' (it has 'header', 'source', 'function', 'includes', 'uses' and 'test')");
        }

        var directory = Path.GetDirectoryName(selectionPath)!;
        var header = PathOf(table, "header", ".hpp", "the path of the generated header", selectionPath, directory);
        var source = PathOf(table, "source", ".cpp", "the path of the generated source", selectionPath, directory);
        var test = table.ContainsKey("test")
                       ? PathOf(table, "test", ".cpp", "the path of the generated defaults test", selectionPath, directory)
                       : null;

        if (!table.TryGetValue("function", out var functionValue) || functionValue is not string function ||
            !QualifiedName().IsMatch(function))
        {
            throw new GenerateException(
                $"{selectionPath}: [registration] 'function' is the qualified name of the registration function " +
                "(Fenrir::Generated::Register<Package>Types)");
        }

        if (!table.TryGetValue("includes", out var includesValue) || includesValue is not TomlArray includeArray ||
            includeArray.Any(include => include is not string))
        {
            throw new GenerateException(
                $"{selectionPath}: [registration] 'includes' is a list of the headers declaring the described types, " +
                "spelled as a source includes them");
        }

        var uses = new List<string>();
        if (table.TryGetValue("uses", out var usesValue))
        {
            if (usesValue is not TomlArray useArray || useArray.Any(use => use is not string path ||
                                                                       !path.EndsWith(Extension, StringComparison.Ordinal)))
            {
                throw new GenerateException(
                    $"{selectionPath}: [registration] 'uses' is a list of the selections ({Extension}) of the packages " +
                    "registering types this description also holds, relative to the selection");
            }

            uses.AddRange(useArray.Cast<string>().Select(use => Path.GetFullPath(Path.Combine(directory, use))));
        }

        var separator = function.LastIndexOf("::", StringComparison.Ordinal);
        return new Registration(selectionPath, fileName[..^Extension.Length], header, source,
                                separator < 0 ? "" : function[..separator], function[(separator < 0 ? 0 : separator + 2)..],
                                includeArray.Cast<string>().ToList(), uses, test);
    }

    private static string PathOf(TomlTable table, string key, string extension, string what, string selectionPath,
                                 string directory)
    {
        if (!table.TryGetValue(key, out var value) || value is not string path ||
            !path.EndsWith(extension, StringComparison.Ordinal))
        {
            throw new GenerateException(
                $"{selectionPath}: [registration] '{key}' is {what} ({extension}), relative to the selection");
        }

        return Path.GetFullPath(Path.Combine(directory, path));
    }

    [GeneratedRegex(@"^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")]
    private static partial Regex QualifiedName();
}
