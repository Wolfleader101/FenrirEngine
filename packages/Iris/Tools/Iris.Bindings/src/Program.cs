namespace Iris.Bindings;

/// <summary>
/// <c>iris-bindings &lt;Name&gt;.binding.toml &lt;Package&gt;.api.json...</c>: writes a binding's C# mirrors, Iris
/// mappings, registration lists and defaults test, each only when its content changes; with <c>[natives]</c>, also the
/// native functions' declarations, registration and test, from the <c>&lt;Name&gt;.natives.json</c> beside the configuration.
/// </summary>
internal static class Program
{
    private const string Usage = "usage: iris-bindings <Name>.binding.toml <Package>.api.json [<Package>.api.json...]";

    public static int Main(string[] args)
    {
        if (args.Length < 2 || args[0] is "-h" or "--help")
        {
            Console.Error.WriteLine(Usage);
            return 2;
        }

        try
        {
            var files = Generator.Generate(args[0], args[1..]);
            var report = Generator.Write(args[0], files);
            Console.WriteLine($"iris-bindings: {args[0]}: {files.Count} files, {report.Count} changed");
            foreach (var line in report)
            {
                Console.WriteLine($"  {line}");
            }

            return 0;
        }
        catch (BindingException exception)
        {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
    }
}
