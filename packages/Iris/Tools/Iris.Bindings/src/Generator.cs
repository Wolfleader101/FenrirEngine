using System.Text;

namespace Iris.Bindings;

/// <summary>The files a binding generates, and writing them.</summary>
internal static class Generator
{
    /// <summary>
    /// Reads the configuration and descriptions (those given, else those the configuration names) and returns every
    /// generated file by absolute path. Throws <see cref="BindingException" /> for anything it cannot bind, a hand-written
    /// field among it.
    /// </summary>
    public static IReadOnlyDictionary<string, string> Generate(string configPath, IEnumerable<string> descriptionPaths)
    {
        var config = BindingConfig.Read(configPath);
        var binding = Load(config, descriptionPaths.Select(Path.GetFullPath).ToList(), []);
        var handWritten = HandWritten.Fields(binding);
        if (handWritten.Count > 0)
        {
            throw new BindingException(string.Join('\n', handWritten));
        }

        var files = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (var (name, text) in CSharpWriter.Mirrors(binding))
        {
            files.Add(Path.Combine(config.CSharp.Generated, name), text);
        }

        NativesDescription? natives = null;
        IReadOnlyList<BoundNative> bound = [];
        if (config.Natives is { } nativesConfig)
        {
            natives = NativesDescription.Read(nativesConfig.Description);
            bound = Natives.Bind(binding, natives);
            if (binding.Mirrored.Any(type => binding.CSharpName(type) == nativesConfig.Class))
            {
                throw new BindingException(
                    $"{config.Path}: [natives] class {nativesConfig.Class} is also the C# name of a mirrored type");
            }

            var (name, text) = CSharpWriter.Natives(binding, natives, bound);
            files.Add(Path.Combine(config.CSharp.Generated, name), text);
        }

        files.Add(config.Cpp.Mappings, CppWriter.Mappings(binding));
        if (config.Registration is { } registration)
        {
            files.Add(registration.Source, CppWriter.Registration(binding, registration, natives, bound));
        }

        if (config.Tests is { } tests)
        {
            files.Add(tests.Cpp, CppWriter.DefaultsTest(binding, tests));
            files.Add(tests.CSharp, CSharpWriter.Defaults(binding, tests, Path.GetFileName(tests.Cpp)));
        }

        return files;
    }

    // A configuration's binding over its descriptions, with the bindings it uses, each read over the descriptions its own
    // configuration names. `chain` is the configurations whose uses led here.
    private static Binding Load(BindingConfig config, IReadOnlyList<string> given, IReadOnlyList<string> chain)
    {
        if (chain.Contains(config.Path, StringComparer.OrdinalIgnoreCase))
        {
            throw new BindingException($"{config.Path}: uses itself, through {string.Join(" and ", chain)}");
        }

        var uses = new List<Binding>();
        foreach (var path in config.Uses)
        {
            if (!File.Exists(path))
            {
                throw new BindingException($"{config.Path}: uses {path}, which does not exist");
            }

            var used = BindingConfig.Read(path);
            if (used.Descriptions.Count == 0)
            {
                throw new BindingException(
                    $"{config.Path}: uses {path}, which names no descriptions: list the descriptions it binds in its 'descriptions'");
            }

            uses.Add(Load(used, [], [.. chain, config.Path]));
        }

        return new Binding(config, Description.Read(Descriptions(config, given)), uses);
    }

    // The descriptions a binding binds: those given, which must be those its configuration names when it names any.
    private static IReadOnlyList<string> Descriptions(BindingConfig config, IReadOnlyList<string> given)
    {
        if (given.Count == 0)
        {
            return config.Descriptions.Count > 0
                       ? config.Descriptions
                       : throw new BindingException(
                             $"{config.Path}: name at least one <Package>.api.json, on the command line or in 'descriptions'");
        }

        if (config.Descriptions.Count > 0 &&
            !config.Descriptions.Order(StringComparer.OrdinalIgnoreCase).SequenceEqual(given.Order(StringComparer.OrdinalIgnoreCase),
                                                                                       StringComparer.OrdinalIgnoreCase))
        {
            throw new BindingException($"{config.Path}: 'descriptions' names {string.Join(", ", config.Descriptions)}, but " +
                                       $"the command line {string.Join(", ", given)}: name the same descriptions");
        }

        return given;
    }

    /// <summary>
    /// Writes each file whose content changed, and deletes any other <c>.cs</c> file in the mirrors' directory, which
    /// the generator owns (a type no longer described). Returns what it did, one line per file.
    /// </summary>
    public static IReadOnlyList<string> Write(string configPath, IReadOnlyDictionary<string, string> files)
    {
        var report = new List<string>();
        var generated = BindingConfig.Read(configPath).CSharp.Generated;
        if (Directory.Exists(generated))
        {
            foreach (var stale in Directory.EnumerateFiles(generated, "*.cs").Where(file => !files.ContainsKey(Path.GetFullPath(file))))
            {
                File.Delete(stale);
                report.Add($"{stale} deleted");
            }
        }

        foreach (var (path, text) in files)
        {
            var bytes = new UTF8Encoding(false).GetBytes(text);
            if (File.Exists(path) && File.ReadAllBytes(path).AsSpan().SequenceEqual(bytes))
            {
                continue;
            }

            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllBytes(path, bytes);
            report.Add($"{path} written");
        }

        return report;
    }
}
