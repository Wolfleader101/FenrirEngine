using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Text.Json.Nodes;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Xunit;

namespace Iris.Bindings.Tests;

public sealed class BindingTests
{
    private static string TestsDirectory([CallerFilePath] string here = "") => Path.GetDirectoryName(here)!;

    private static string FixtureDirectory => Path.Combine(TestsDirectory(), "Fixture");

    private static string FixtureConfig => Path.Combine(FixtureDirectory, "Fixture.binding.toml");

    private static string FixtureDescription => Path.Combine(FixtureDirectory, "Fixture.api.json");

    private static CSharpParseOptions Latest => CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.Latest);

    private static CancellationToken Cancel => TestContext.Current.CancellationToken;

    [Fact]
    public void TheFixtureGivesTheCommittedFilesByteForByte()
    {
        var generated = Generator.Generate(FixtureConfig, [FixtureDescription]);

        foreach (var (path, text) in generated)
        {
            Assert.True(File.Exists(path), $"{path} is generated but not committed. Run the bindings target and commit it.");
            var committed = File.ReadAllText(path);
            Assert.True(committed == text,
                        $"{path} differs from what the fixture generates. If the change is meant, regenerate it (the " +
                        "bindings target) and review the diff.\n" + FirstDifference(committed, text));
        }

        var mirrors = Path.Combine(FixtureDirectory, "Contract", "Generated");
        foreach (var file in Directory.EnumerateFiles(mirrors, "*.cs"))
        {
            Assert.True(generated.ContainsKey(Path.GetFullPath(file)), $"{file} is committed but no longer generated.");
        }

        Assert.True(generated.Count == 18,
                    $"The fixture should give 14 mirrors, the mappings, the registration and the two defaults test " +
                    $"files, but gave {generated.Count} files.");
    }

    [Fact]
    public void AChangedDefaultChangesExactlyItsInitialiser()
    {
        using var directory = new TemporaryDirectory();
        var description = WriteDescription(directory, root => Field(root, "Fixture::Spring", "damping")["default"] = 0.25);

        var before = Generator.Generate(FixtureConfig, [FixtureDescription]);
        var after = Generator.Generate(FixtureConfig, [description]);

        var changed = Changes(before, after);
        Assert.True(changed.SequenceEqual(["Spring.cs: '    public float Damping = 0.5f;' became '    public float Damping = 0.25f;'"]),
                    "Changing Spring::damping's default should change only its C# initialiser, but changed:\n" +
                    string.Join('\n', changed));
    }

    [Fact]
    public void RunningTwiceRewritesNothing()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "");
        var description = WriteDescription(directory, _ => { });

        var first = Generator.Write(config, Generator.Generate(config, [description]));
        var second = Generator.Write(config, Generator.Generate(config, [description]));

        Assert.True(first.Count == 18, $"The first run should write every file, but reported:\n{string.Join('\n', first)}");
        Assert.True(second.Count == 0, $"A second run should write nothing, but reported:\n{string.Join('\n', second)}");
    }

    [Fact]
    public void AMirrorNoLongerDescribedIsDeleted()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "");
        var description = WriteDescription(directory, _ => { });
        var stale = Path.Combine(directory.Path, "Contract", "Generated", "Removed.cs");
        Directory.CreateDirectory(Path.GetDirectoryName(stale)!);
        File.WriteAllText(stale, "// A mirror of a type removed from the description.\n");

        Generator.Write(config, Generator.Generate(config, [description]));

        Assert.False(File.Exists(stale), "A generated mirror whose type is no longer described should be deleted.");
    }

    [Fact]
    public void AHandWrittenFieldBesideAGeneratedMirrorFailsNamingTheStructAndField()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "");
        var description = WriteDescription(directory, _ => { });
        var contract = Path.Combine(directory.Path, "Contract");
        Directory.CreateDirectory(contract);
        File.WriteAllText(Path.Combine(contract, "Spring.cs"),
                          "namespace Fixture.Bindings;\n\npublic partial struct Spring\n{\n    public float Extra;\n" +
                          "    public int Count { get; set; }\n    public static int Made;\n    public float Total => Extra;\n}\n");

        var exception = Assert.Throws<BindingException>(() => Generator.Generate(config, [description]));

        Assert.True(exception.Message.Contains("Spring.cs:5: error: Fixture.Bindings.Spring declares the field 'Extra' by hand",
                                               StringComparison.Ordinal) &&
                    exception.Message.Contains("declares the field 'Count' by hand", StringComparison.Ordinal),
                    "A hand-written field and auto-property in a generated mirror's partial should each fail, naming the " +
                    $"struct and member, but the failure was:\n{exception.Message}");
        Assert.False(exception.Message.Contains("'Made'", StringComparison.Ordinal) ||
                     exception.Message.Contains("'Total'", StringComparison.Ordinal),
                     $"A static field and a computed property add no layout and should pass, but:\n{exception.Message}");
        Assert.False(Directory.Exists(Path.Combine(contract, "Generated")), "A failed run should write nothing.");
    }

    [Theory]
    [InlineData("DefaultKindNotZero",
                "Fixture::Shape's default kind is m_sphere, but a tagged union's C# new() makes the kind whose tag value is 0 (Box)")]
    [InlineData("UnboundType", "Fixture::Quat is a bound type (")]
    [InlineData("FieldNamedAfterItsType",
                "Fixture::Spring::spring: C# cannot name a field Spring inside Spring; give it another name in [names]")]
    [InlineData("OtherTarget", "the description is for another target")]
    public void WhatCannotBeBoundFailsSayingWhy(string problem, string diagnostic)
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, problem == "UnboundType"
                                               ? "\"Fixture::Vec3\" = { type = \"Vector3\" }\n\"Fixture::Handle\" = { type = \"Handle\" }\n"
                                               : "");
        var description = WriteDescription(directory, root =>
        {
            switch (problem)
            {
                case "DefaultKindNotZero":
                    Type(root, "Fixture::Shape")["defaultKind"] = "m_sphere";
                    break;
                case "FieldNamedAfterItsType":
                    Field(root, "Fixture::Spring", "damping")["name"] = "spring";
                    break;
                case "OtherTarget":
                    root["pointerSize"] = 4;
                    break;
            }
        });

        var exception = Assert.Throws<BindingException>(() => Generator.Generate(config, [description]));

        Assert.True(exception.Message.Contains(diagnostic, StringComparison.Ordinal),
                    $"Binding should fail with\n  {diagnostic}\nbut failed with\n  {exception.Message}");
    }

    [Fact]
    public void TheMirrorsAndDefaultsClassCompileAgainstStubsOfTheSdk()
    {
        var sources = Directory.EnumerateFiles(Path.Combine(FixtureDirectory, "Contract"), "*.cs", SearchOption.AllDirectories)
                          .Concat(Directory.EnumerateFiles(Path.Combine(FixtureDirectory, "Tests", "Generated"), "*.cs"))
                          .Append(Path.Combine(TestsDirectory(), "Checks", "Stubs.cs"))
                          .Select(path => CSharpSyntaxTree.ParseText(File.ReadAllText(path), Latest, path, cancellationToken: Cancel))
                          .Append(CSharpSyntaxTree.ParseText("global using System;", Latest, cancellationToken: Cancel));
        var references = ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!)
                             .Split(Path.PathSeparator)
                             .Select(path => MetadataReference.CreateFromFile(path));
        var compilation = CSharpCompilation.Create(
            "Iris.Bindings.StubCheck", sources, references,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, nullableContextOptions: NullableContextOptions.Enable));

        using var assembly = new MemoryStream();
        var result = compilation.Emit(assembly, cancellationToken: Cancel);

        var problems = result.Diagnostics.Where(diagnostic => diagnostic.Severity >= DiagnosticSeverity.Warning).ToList();
        Assert.True(result.Success && problems.Count == 0,
                    "The generated C# should compile against the stubs with no warning:\n" + string.Join('\n', problems));
    }

    [Fact]
    public async Task TheMirrorsBuildAgainstTheScriptingSdkWithNoDiagnostic()
    {
        var project = Path.Combine(TestsDirectory(), "Checks", "Sdk", "Iris.Bindings.SdkCheck.csproj");
        var sdk = Path.GetFullPath(Path.Combine(TestsDirectory(), "../../../../FenrirScripting/Managed/Fenrir.Scripting.InRepo.props"));
        if (!File.Exists(sdk))
        {
            Assert.Skip($"The Fenrir scripting SDK is not beside Iris ({sdk}), so only the stubs are compiled against.");
        }

        var start = new ProcessStartInfo("dotnet", ["build", project, "--nologo", "-v:q", "-warnaserror", "--no-incremental"])
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        using var build = Process.Start(start)!;
        var output = build.StandardOutput.ReadToEndAsync(Cancel);
        var errors = build.StandardError.ReadToEndAsync(Cancel);
        await build.WaitForExitAsync(Cancel);

        Assert.True(build.ExitCode == 0,
                    "The generated mirrors should build against the scripting SDK, its tagged-union, component and event " +
                    $"generators included, with no diagnostic:\n{await output}\n{await errors}");
    }

    // A copy of the fixture's configuration in the directory, so every output lands there. `boundTypes` replaces the
    // entries of [bound-types] when not empty.
    private static string CopyConfig(TemporaryDirectory directory, string boundTypes)
    {
        var text = File.ReadAllText(FixtureConfig);
        if (boundTypes.Length > 0)
        {
            var start = text.IndexOf("[bound-types]", StringComparison.Ordinal);
            var end = text.IndexOf("\n\n", start, StringComparison.Ordinal);
            text = text[..start] + "[bound-types]\n" + boundTypes + text[end..];
        }

        var config = Path.Combine(directory.Path, "Fixture.binding.toml");
        File.WriteAllText(config, text);
        return config;
    }

    // The fixture's description, edited, written as Fixture.api.json in the directory (the generated files name it).
    private static string WriteDescription(TemporaryDirectory directory, Action<JsonNode> edit)
    {
        var root = JsonNode.Parse(File.ReadAllText(FixtureDescription))!;
        edit(root);
        var path = Path.Combine(directory.Path, "Fixture.api.json");
        File.WriteAllText(path, root.ToJsonString());
        return path;
    }

    private static JsonNode Type(JsonNode root, string name) =>
        root["types"]!.AsArray().First(type => (string)type!["name"]! == name)!;

    private static JsonNode Field(JsonNode root, string type, string field) =>
        Type(root, type)["fields"]!.AsArray().First(node => (string)node!["name"]! == field)!;

    // Every line that differs between two runs over the same configuration, by file name.
    private static List<string> Changes(IReadOnlyDictionary<string, string> before, IReadOnlyDictionary<string, string> after)
    {
        var changes = new List<string>();
        foreach (var (path, text) in after)
        {
            var old = before[path].Split('\n');
            var lines = text.Split('\n');
            for (var index = 0; index < Math.Max(old.Length, lines.Length); index++)
            {
                var was = index < old.Length ? old[index] : "";
                var now = index < lines.Length ? lines[index] : "";
                if (was != now)
                {
                    changes.Add($"{Path.GetFileName(path)}: '{was}' became '{now}'");
                }
            }
        }

        return changes;
    }

    private static string FirstDifference(string expected, string actual)
    {
        var expectedLines = expected.Split('\n');
        var actualLines = actual.Split('\n');
        for (var index = 0; index < Math.Max(expectedLines.Length, actualLines.Length); index++)
        {
            var committed = index < expectedLines.Length ? expectedLines[index] : "<end of file>";
            var generated = index < actualLines.Length ? actualLines[index] : "<end of file>";
            if (committed != generated)
            {
                return $"First difference, line {index + 1}:\n  committed: {committed}\n  generated: {generated}";
            }
        }

        return "";
    }

    private sealed class TemporaryDirectory : IDisposable
    {
        public TemporaryDirectory()
        {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "iris-bindings-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public void Dispose() => Directory.Delete(Path, true);
    }
}
