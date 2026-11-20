using System.Text;

namespace GenerateTypeRegistry;

/// <summary>
/// <c>generate-type-registry &lt;Package&gt;.api.toml</c>: reads the selection's
/// <c>[registration]</c> table and the package's <c>&lt;Package&gt;.api.json</c> beside it, and writes the header and
/// source the table names, each only when its content changes. A selection without the table writes nothing.
/// </summary>
internal static class Program
{
    private const string Usage = "usage: generate-type-registry <Package>.api.toml";

    public static int Main(string[] args)
    {
        if (args.Length != 1 || args[0] is "-h" or "--help")
        {
            Console.Error.WriteLine(Usage);
            return 2;
        }

        try
        {
            var registration = Registration.Read(args[0]);
            if (registration is null)
            {
                Console.WriteLine($"generate-type-registry: {Path.GetFullPath(args[0])} has no [registration]: nothing written");
                return 0;
            }

            if (!File.Exists(registration.DescriptionPath))
            {
                throw new GenerateException(
                    $"{registration.DescriptionPath}: no description: run fenrir-describe-api first");
            }

            var generated = Generator.Generate(registration, File.ReadAllText(registration.DescriptionPath));
            foreach (var (path, text) in new[] { (registration.HeaderPath, generated.Header),
                                                 (registration.SourcePath, generated.Source) })
            {
                var written = WriteIfChanged(path, text);
                Console.WriteLine($"generate-type-registry: {path} {(written ? "written" : "unchanged")}");
            }

            return 0;
        }
        catch (GenerateException exception)
        {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
    }

    /// <summary>Writes the text unless the file already holds it, so an unchanged registration keeps its timestamp.</summary>
    private static bool WriteIfChanged(string path, string text)
    {
        var bytes = new UTF8Encoding(false).GetBytes(text);
        if (File.Exists(path) && File.ReadAllBytes(path).AsSpan().SequenceEqual(bytes))
        {
            return false;
        }

        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, bytes);
        return true;
    }
}
