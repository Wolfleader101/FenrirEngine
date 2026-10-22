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

    private static string FixtureNatives => Path.Combine(FixtureDirectory, "Fixture.natives.json");

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

        Assert.True(generated.Count == 21,
                    $"The fixture should give 14 mirrors, the natives' class, the mappings, the registration and the " +
                    $"two files each of the defaults and natives tests, but gave {generated.Count} files.");
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
    public void AMirrorWhoseDefaultsAreAllZeroHasNoConstructorOrInitialisers()
    {
        using var directory = new TemporaryDirectory();
        var description = WriteDescription(directory, root =>
        {
            Field(root, "Fixture::Spring", "stiffness")["default"] = 0;
            Field(root, "Fixture::Spring", "damping")["default"] = 0;
        });

        var spring = Mirror(Generator.Generate(FixtureConfig, [description]), "Spring.cs");

        Assert.True(spring.Contains("    public float Stiffness;\n", StringComparison.Ordinal) &&
                    spring.Contains("    public float Damping;\n", StringComparison.Ordinal) &&
                    !spring.Contains("public Spring()", StringComparison.Ordinal),
                    "With every default zero, C#'s default already holds them, so Spring should have bare fields and no " +
                    $"constructor, but is:\n{spring}");
    }

    [Fact]
    public void AReadOnlyMirrorKeepsItsConstructorWhateverItsDefaults()
    {
        using var directory = new TemporaryDirectory();
        var description = WriteDescription(directory, root =>
        {
            foreach (var field in Type(root, "Fixture::Limits")["fields"]!.AsArray())
            {
                field!["default"] = 0;
            }
        });

        var limits = Mirror(Generator.Generate(FixtureConfig, [description]), "Limits.cs");

        Assert.True(limits.Contains("public readonly partial struct Limits", StringComparison.Ordinal) &&
                    limits.Contains("    public readonly float Infinity = 0;\n", StringComparison.Ordinal) &&
                    limits.Contains("public Limits()", StringComparison.Ordinal),
                    $"Limits is in readonly-structs, so it should keep its readonly fields and constructor, but is:\n{limits}");
    }

    [Fact]
    public void ABoundTypesOwnDefaultIsTheConfiguredExpression()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "\"Fixture::Vec3\" = { type = \"Vector3\", constructor = [\"x\", \"y\", \"z\"] }\n" +
                                           "\"Fixture::Quat\" = { type = \"Quaternion\" }\n" +
                                           "\"Fixture::Handle\" = { type = \"Handle\", default = \"Handle.None\" }\n");
        var description = WriteDescription(directory, _ => { });

        var body = Mirror(Generator.Generate(config, [description]), "Body.cs");

        Assert.True(body.Contains("    public Handle Owner = Handle.None;\n", StringComparison.Ordinal),
                    $"A field holding a bound type's own default should be its configured default, but Body is:\n{body}");
    }

    [Fact]
    public void AnIntegralComponentOfABoundTypeIsWrittenAsTheInteger()
    {
        using var directory = new TemporaryDirectory();
        var description = WriteDescription(directory, root =>
            Field(root, "Fixture::Body", "offset")["default"] = JsonNode.Parse("""{"x": 16777217, "y": 2, "z": 3.5}"""));

        var body = Mirror(Generator.Generate(FixtureConfig, [description]), "Body.cs");

        Assert.True(body.Contains("    public Vector3 Offset = new(16777217, 2, 3.5f);\n", StringComparison.Ordinal),
                    "A bound type's components have no described kind, so an integer should stay the integer it is (a " +
                    $"float would round 16777217), but Body is:\n{body}");
    }

    [Fact]
    public void AnExcludedTypeGetsNoMirrorMappingRegistrationOrTest()
    {
        using var directory = new TemporaryDirectory();
        // Without natives: one of them takes a Hit.
        var config = CopyConfig(directory, "", "exclude = [\"Fixture::Hit\"]\n", WithoutNatives);
        var description = WriteDescription(directory, _ => { });

        var generated = Generator.Generate(config, [description]);

        Assert.False(generated.Keys.Any(path => Path.GetFileName(path) == "Hit.cs"), "An excluded type should get no mirror.");
        var named = generated.Where(file => file.Value.Contains("Fixture::Hit", StringComparison.Ordinal) ||
                                            file.Value.Contains("Fixture.Bindings.Hit", StringComparison.Ordinal))
                             .Select(file => Path.GetFileName(file.Key))
                             .ToList();
        Assert.True(named.Count == 0,
                    $"An excluded type should be in no mapping, registration or test, but these name it: {string.Join(", ", named)}");
    }

    [Theory]
    [InlineData("exclude = [\"Fixture::Spring\"]\n",
                "exclude names Fixture::Spring, but Fixture::Body::spring holds one: exclude Fixture::Body too")]
    [InlineData("exclude = [\"Fixture::Missing\"]\n", "exclude names Fixture::Missing, which no description describes")]
    public void AnExclusionTheBindingCannotHonourFailsSayingWhy(string exclude, string diagnostic)
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "", exclude);
        var description = WriteDescription(directory, _ => { });

        var exception = Assert.Throws<BindingException>(() => Generator.Generate(config, [description]));

        Assert.True(exception.Message.Contains(diagnostic, StringComparison.Ordinal),
                    $"Binding should fail with\n  {diagnostic}\nbut failed with\n  {exception.Message}");
    }

    [Fact]
    public void AReadOnlyStructThatIsNotADescribedStructFails()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "", edit: text => text.Replace("readonly-structs = [\"Fixture::Limits\"]",
                                                                         "readonly-structs = [\"Fixture::Motion\"]"));
        var description = WriteDescription(directory, _ => { });

        var exception = Assert.Throws<BindingException>(() => Generator.Generate(config, [description]));

        Assert.True(exception.Message.Contains("readonly-structs names Fixture::Motion, which is not a described struct",
                                               StringComparison.Ordinal),
                    $"An enum in readonly-structs should fail naming it, but failed with\n  {exception.Message}");
    }

    [Fact]
    public void RunningTwiceRewritesNothing()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "");
        var description = WriteDescription(directory, _ => { });

        var first = Generator.Write(config, Generator.Generate(config, [description]));
        var second = Generator.Write(config, Generator.Generate(config, [description]));

        Assert.True(first.Count == 21,$"The first run should write every file, but reported:\n{string.Join('\n', first)}");
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
    public void ANativeAddedWithAnOutputIsDeclaredRegisteredAndTested()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "");
        var description = WriteDescription(directory, _ => { });
        var before = Generator.Generate(config, [description]);
        WriteNatives(directory, root =>
        {
            var added = Native(root, "Find").DeepClone();
            added["name"] = "Nearest";
            root["functions"]!.AsArray().Add(added);
        });

        var after = Generator.Generate(config, [description]);

        var added = Changes(before, after).Where(change => change.Contains("Nearest", StringComparison.Ordinal)).ToList();
        string[] expected =
        [
            "FixtureNative.cs: '' became '    public static partial bool Nearest(in Vector3 origin, float distance, out Hit hit);'",
            "FixtureRegistration.cpp: '} // namespace Fixture::Natives' became '    bool Nearest(const Host::CallContext&, Host::State&, const Fixture::Vec3* origin, float distance, Fixture::Hit* hit);'",
            "FixtureRegistration.cpp: '' became '            Host::NativeFunction::Make<&Fixture::Natives::Nearest>(\"Fixture.Bindings.FixtureNative.Nearest\"),'",
            "FixtureNatives.cs: '' became '    public static bool Nearest()'",
            "FixtureNativesTests.cpp: '' became 'TEST_CASE(\"Fixture.Bindings.FixtureNative.Nearest, given nothing to act on, writes its output's default\", \"[bindings]\")'",
        ];
        foreach (var line in expected)
        {
            Assert.True(added.Any(change => change.EndsWith(line[(line.IndexOf("became", StringComparison.Ordinal))..], StringComparison.Ordinal) &&
                                            change.StartsWith(line[..line.IndexOf(':')], StringComparison.Ordinal)),
                        $"A native with an output, added to the description, should give\n  {line}\nbut the changes naming it were:\n  " +
                        string.Join("\n  ", added));
        }
    }

    [Theory]
    [InlineData("ByValue", "Fixture.natives.json: Find::origin: cannot map the C++ type 'Fixture::Spring' to C#: Iris " +
                           "passes no struct or enum by value: take a const pointer")]
    [InlineData("Undescribed", "Fixture.natives.json: Find::origin: cannot map the C++ type 'const Fixture::Unknown*' to " +
                               "C#: no description describes Fixture::Unknown, and [natives] types does not name it")]
    [InlineData("NeitherOutNorSpan", "Fixture.natives.json: Find::hit: cannot map the C++ type 'Fixture::Hit*' to C#: a " +
                                     "pointer to non-const data is an output (its doc says [out]) or a span (its " +
                                     "std::int32_t count follows it)")]
    [InlineData("Result", "Fixture.natives.json: Sum (result): cannot map the C++ type 'const Fixture::Vec3*' to C#: a " +
                          "native returns a number, a bool or nothing")]
    public void ANativeParameterTheBindingCannotMapFailsNamingTheFunctionAndParameter(string problem, string diagnostic)
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "");
        var description = WriteDescription(directory, _ => { });
        WriteNatives(directory, root =>
        {
            switch (problem)
            {
                case "ByValue":
                    Parameter(root, "Find", "origin").ReplaceWith(JsonNode.Parse(
                        """{ "name": "origin", "type": "Fixture::Spring", "kind": "struct" }"""));
                    break;
                case "Undescribed":
                    Parameter(root, "Find", "origin")["type"] = "const Fixture::Unknown*";
                    Parameter(root, "Find", "origin")["pointee"] = "Fixture::Unknown";
                    break;
                case "NeitherOutNorSpan":
                    Parameter(root, "Find", "hit").AsObject().Remove("output");
                    break;
                case "Result":
                    Native(root, "Sum")["returns"] = JsonNode.Parse(
                        """{ "type": "const Fixture::Vec3*", "kind": "pointer", "pointee": "Fixture::Vec3", "pointeeKind": "struct", "const": true }""");
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
        // The natives' class has no bodies until the SDK's generator writes them: the SDK check compiles it.
        var sources = Directory.EnumerateFiles(Path.Combine(FixtureDirectory, "Contract"), "*.cs", SearchOption.AllDirectories)
                          .Where(path => Path.GetFileName(path) != "FixtureNative.cs")
                          .Concat(Directory.EnumerateFiles(Path.Combine(FixtureDirectory, "Tests", "Generated"), "*.cs"))
                          .Append(Path.Combine(TestsDirectory(), "Checks", "Stubs.cs"))
                          .Select(path => CSharpSyntaxTree.ParseText(File.ReadAllText(path), Latest, path, cancellationToken: Cancel))
                          .Append(CSharpSyntaxTree.ParseText("global using System;", Latest, cancellationToken: Cancel));
        var references = ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!)
                             .Split(Path.PathSeparator)
                             .Select(path => MetadataReference.CreateFromFile(path));
        var compilation = CSharpCompilation.Create(
            "Iris.Bindings.StubCheck", sources, references,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, nullableContextOptions: NullableContextOptions.Enable,
                                         allowUnsafe: true));

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
    // entries of [bound-types] when not empty; `topLevel` is added after the namespace, and `edit` applied last.
    private static string CopyConfig(TemporaryDirectory directory, string boundTypes, string topLevel = "",
                                     Func<string, string>? edit = null)
    {
        var text = File.ReadAllText(FixtureConfig);
        var namespaceLine = text.IndexOf('\n', text.IndexOf("namespace = ", StringComparison.Ordinal)) + 1;
        text = text[..namespaceLine] + topLevel + text[namespaceLine..];
        if (boundTypes.Length > 0)
        {
            var start = text.IndexOf("[bound-types]", StringComparison.Ordinal);
            var end = text.IndexOf("\n\n", start, StringComparison.Ordinal);
            text = text[..start] + "[bound-types]\n" + boundTypes + text[end..];
        }

        var config = Path.Combine(directory.Path, "Fixture.binding.toml");
        File.WriteAllText(config, edit == null ? text : edit(text));
        File.Copy(FixtureNatives, Path.Combine(directory.Path, "Fixture.natives.json"), overwrite: true);
        return config;
    }

    // A configuration's text without [natives] or the natives test.
    private static string WithoutNatives(string text)
    {
        var start = text.IndexOf("[natives]", StringComparison.Ordinal);
        var end = text.IndexOf("\n\n", start, StringComparison.Ordinal);
        text = text[..start] + text[end..];
        return string.Join('\n', text.Split('\n').Where(line => !line.StartsWith("natives-", StringComparison.Ordinal)));
    }

    // The fixture's natives description, edited, written as Fixture.natives.json beside a copied configuration.
    private static void WriteNatives(TemporaryDirectory directory, Action<JsonNode> edit)
    {
        var root = JsonNode.Parse(File.ReadAllText(FixtureNatives))!;
        edit(root);
        File.WriteAllText(Path.Combine(directory.Path, "Fixture.natives.json"), root.ToJsonString());
    }

    private static JsonNode Native(JsonNode root, string name) =>
        root["functions"]!.AsArray().First(function => (string)function!["name"]! == name)!;

    private static JsonNode Parameter(JsonNode root, string function, string parameter) =>
        Native(root, function)["parameters"]!.AsArray().First(node => (string)node!["name"]! == parameter)!;

    // The generated mirror of that file name.
    private static string Mirror(IReadOnlyDictionary<string, string> generated, string fileName) =>
        generated.Single(file => Path.GetFileName(file.Key) == fileName).Value;

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
