using System.Text;

namespace DescribeApi;

/// <summary>
/// <c>describe-api &lt;Package&gt;.api.toml [compile arguments...]</c>: writes <c>&lt;Package&gt;.api.json</c> beside the
/// selection when its content changes. <c>describe-api --natives &lt;Name&gt;.binding.toml [compile arguments...]</c>:
/// writes the native functions of the configuration's <c>[natives]</c> sources to <c>&lt;Name&gt;.natives.json</c> beside
/// it. An argument <c>@file</c> reads more arguments from a file, one per line.
/// </summary>
internal static class Program
{
    private const string Usage =
        "usage: describe-api <Package>.api.toml [compile arguments, or @file with one per line]\n" +
        "       describe-api --natives <Name>.binding.toml [compile arguments, or @file with one per line]";

    public static int Main(string[] args)
    {
        if (args.Length == 0 || args[0] is "-h" or "--help")
        {
            Console.Error.WriteLine(Usage);
            return 2;
        }

        try
        {
            if (args[0] == "--natives")
            {
                if (args.Length < 2)
                {
                    Console.Error.WriteLine(Usage);
                    return 2;
                }

                var natives = NativesSelection.Read(args[1]);
                var described = NativesDescriber.Describe(natives, ExpandResponseFiles(args[2..]));
                var changed = WriteIfChanged(natives.DescriptionPath, NativesWriter.Write(described));
                Console.WriteLine($"describe-api: {natives.DescriptionPath} {(changed ? "written" : "unchanged")}");
                return 0;
            }

            var selection = Selection.Read(args[0]);
            var description = Describer.Describe(selection, ExpandResponseFiles(args[1..]));
            var written = WriteIfChanged(selection.DescriptionPath, DescriptionWriter.Write(description));
            Console.WriteLine($"describe-api: {selection.DescriptionPath} {(written ? "written" : "unchanged")}");
            return 0;
        }
        catch (DescribeException exception)
        {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
    }

    /// <summary>Writes the text unless the file already holds it, so an unchanged description keeps its timestamp.</summary>
    public static bool WriteIfChanged(string path, string text)
    {
        var bytes = new UTF8Encoding(false).GetBytes(text);
        if (File.Exists(path) && File.ReadAllBytes(path).AsSpan().SequenceEqual(bytes))
        {
            return false;
        }

        File.WriteAllBytes(path, bytes);
        return true;
    }

    private static List<string> ExpandResponseFiles(IEnumerable<string> arguments)
    {
        var expanded = new List<string>();
        foreach (var argument in arguments)
        {
            if (argument.StartsWith('@'))
            {
                expanded.AddRange(File.ReadAllLines(argument[1..]).Select(line => line.Trim()).Where(line => line.Length > 0));
            }
            else
            {
                expanded.Add(argument);
            }
        }

        return expanded;
    }
}
