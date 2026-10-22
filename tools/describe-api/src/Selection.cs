using Tomlyn;
using Tomlyn.Model;

namespace DescribeApi;

/// <summary>
/// A package's selection (<c>&lt;Package&gt;.api.toml</c>): the headers to read and the types to share, each with its
/// role, and the selected types whose private data members are shared all the same. Header paths are relative to the
/// selection's own directory.
/// </summary>
internal sealed record Selection(string Path, string Package, IReadOnlyList<string> Headers,
                                 IReadOnlyList<KeyValuePair<string, Role>> Types, IReadOnlySet<string> PrivateMembers)
{
    public const string Extension = ".api.toml";

    /// <summary>Where the description is written: <c>&lt;Package&gt;.api.json</c> beside the selection.</summary>
    public string DescriptionPath =>
        System.IO.Path.Combine(System.IO.Path.GetDirectoryName(Path)!, Package + ".api.json");

    /// <summary>Reads a selection file, or throws <see cref="DescribeException" /> saying what is wrong with it.</summary>
    public static Selection Read(string path)
    {
        path = System.IO.Path.GetFullPath(path);
        var fileName = System.IO.Path.GetFileName(path);
        if (!fileName.EndsWith(Extension, StringComparison.Ordinal) || fileName.Length == Extension.Length)
        {
            throw new DescribeException($"{path}: a selection is named <Package>{Extension}");
        }

        var package = fileName[..^Extension.Length];
        TomlTable table;
        try
        {
            table = Toml.ToModel(File.ReadAllText(path), path);
        }
        catch (TomlException exception)
        {
            throw new DescribeException($"{path}: {exception.Message}");
        }

        foreach (var key in table.Keys)
        {
            if (key is not ("headers" or "types" or "private-members"))
            {
                throw new DescribeException(
                    $"{path}: unknown key '{key}' (a selection has 'headers', 'types' and 'private-members')");
            }
        }

        if (!table.TryGetValue("headers", out var headersValue) || headersValue is not TomlArray headerArray ||
            headerArray.Count == 0 || headerArray.Any(header => header is not string))
        {
            throw new DescribeException($"{path}: 'headers' is a list of header paths, relative to the selection");
        }

        var directory = System.IO.Path.GetDirectoryName(path)!;
        var headers = headerArray.Cast<string>()
                          .Select(header => System.IO.Path.GetFullPath(System.IO.Path.Combine(directory, header)))
                          .ToList();
        foreach (var header in headers.Where(header => !File.Exists(header)))
        {
            throw new DescribeException($"{path}: header '{header}' does not exist");
        }

        if (!table.TryGetValue("types", out var typesValue) || typesValue is not TomlTable typeTable ||
            typeTable.Count == 0)
        {
            throw new DescribeException(
                $"{path}: 'types' is a table of qualified type names, each set to its role");
        }

        var types = new List<KeyValuePair<string, Role>>();
        foreach (var (name, roleValue) in typeTable)
        {
            if (roleValue is not string spelling || Roles.Parse(spelling) is not { } role)
            {
                throw new DescribeException(
                    $"{path}: type '{name}' has role '{roleValue}'; a role is component, event, shared-struct or bound-type");
            }

            types.Add(new(name, role));
        }

        var privateMembers = new HashSet<string>(StringComparer.Ordinal);
        if (table.TryGetValue("private-members", out var privateValue))
        {
            if (privateValue is not TomlArray privateArray || privateArray.Any(item => item is not string))
            {
                throw new DescribeException($"{path}: 'private-members' is a list of selected type names");
            }

            foreach (var name in privateArray.Cast<string>())
            {
                if (types.All(type => type.Key != name) || types.First(type => type.Key == name).Value == Role.BoundType)
                {
                    throw new DescribeException(
                        $"{path}: 'private-members' names {name}, which [types] does not select as a component, event or shared struct");
                }

                privateMembers.Add(name);
            }
        }

        return new Selection(path, package, headers, types, privateMembers);
    }
}
