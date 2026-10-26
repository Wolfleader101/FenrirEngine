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

    private static string UsesConfig => Path.Combine(FixtureDirectory, "Uses", "Common.binding.toml");

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

        Assert.True(generated.Count == 26,
                    $"The fixture should give 19 mirrors, the natives' class, the mappings, the registration and the " +
                    $"two files each of the defaults and natives tests, but gave {generated.Count} files.");
    }

    [Fact]
    public void AChangedDefaultChangesExactlyItsInitialiser()
    {
        using var directory = new TemporaryDirectory();
        var description = WriteDescription(directory, root => Field(root, "Fixture::SphereShape", "radius")["default"] = 0.25);

        var before = Generator.Generate(FixtureConfig, [FixtureDescription]);
        var after = Generator.Generate(FixtureConfig, [description]);

        var changed = Changes(before, after);
        Assert.True(changed.SequenceEqual(["SphereShape.cs: '    public float Radius = 0.5f;' became '    public float Radius = 0.25f;'"]),
                    "Changing SphereShape::radius's default should change only its C# initialiser, but changed:\n" +
                    string.Join('\n', changed));
    }

    [Fact]
    public void AMirrorWhoseDefaultsAreAllZeroHasNoConstructorOrInitialisers()
    {
        using var directory = new TemporaryDirectory();
        var description = WriteDescription(directory, root => Field(root, "Fixture::SphereShape", "radius")["default"] = 0);

        var sphere = Mirror(Generator.Generate(FixtureConfig, [description]), "SphereShape.cs");

        Assert.True(sphere.Contains("    public float Radius;\n", StringComparison.Ordinal) &&
                    !sphere.Contains("public SphereShape()", StringComparison.Ordinal),
                    "With every default zero, C#'s default already holds them, so SphereShape should have a bare field " +
                    $"and no constructor, but is:\n{sphere}");
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
                                           "\"Fixture::Handle\" = { type = \"Handle\", default = \"Handle.None\" }\n" +
                                           "\"Fixture::Size\" = { type = \"Size\" }\n" +
                                           "\"Fixture::Ticket\" = { type = \"Ticket\", wrapper = \"u32\" }\n");
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
    [InlineData("exclude = [\"Fixture::Numbers\"]\n",
                "exclude names Fixture::Numbers, but Fixture::Body::numbers holds one: exclude Fixture::Body too")]
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

    [Theory]
    [InlineData("private-fields = [\"Fixture::Tag::m_name\"]",
                "private-fields names Fixture::Tag::m_name, which is not a field of a described struct")]
    [InlineData("fixed-buffers = [\"Fixture::Tag::m_id\"]", "fixed-buffers names Fixture::Tag::m_id, which is not an array of numbers")]
    [InlineData("fixed-buffers = [\"Fixture::Body::springs\"]",
                "fixed-buffers names Fixture::Body::springs, which is not an array of numbers")]
    [InlineData("fixed-buffers = [\"Fixture::Tag::m_label\"]\nreadonly-structs = [\"Fixture::Limits\", \"Fixture::Tag\"]",
                "fixed-buffers names Fixture::Tag::m_label, but Fixture::Tag's mirror is read-only and a fixed buffer cannot be")]
    public void AFieldListTheBindingCannotHonourFailsNamingTheField(string lists, string diagnostic)
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "", edit: text =>
        {
            var start = text.IndexOf("private-fields = ", StringComparison.Ordinal);
            var end = text.IndexOf('\n', text.IndexOf("fixed-buffers = ", StringComparison.Ordinal)) + 1;
            text = text[..start] + lists + "\n" + text[end..];
            return lists.Contains("readonly-structs", StringComparison.Ordinal)
                       ? text.Replace("readonly-structs = [\"Fixture::Limits\"]\n", "", StringComparison.Ordinal)
                       : text;
        });
        var description = WriteDescription(directory, _ => { });

        var exception = Assert.Throws<BindingException>(() => Generator.Generate(config, [description]));

        Assert.True(exception.Message.Contains(diagnostic, StringComparison.Ordinal),
                    $"Binding should fail with\n  {diagnostic}\nbut failed with\n  {exception.Message}");
    }

    [Fact]
    public void ARegisteredNamespaceNamesTheComponentsAndEventsButNotTheSharedStructsOrMirrors()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "", "registered-namespace = \"Fixture\"\n");
        var description = WriteDescription(directory, _ => { });

        var generated = Generator.Generate(config, [description]);

        var registration = generated.Single(file => file.Key.EndsWith("FixtureRegistration.cpp", StringComparison.Ordinal)).Value;
        var body = Mirror(generated, "Body.cs");
        var touched = Mirror(generated, "Touched.cs");
        var mappings = generated.Single(file => file.Key.EndsWith("FixtureMappings.hpp", StringComparison.Ordinal)).Value;
        Assert.True(registration.Contains("Host::MakeComponent<Fixture::Body>(\"Fixture.Body\")", StringComparison.Ordinal) &&
                    registration.Contains("Host::MakeEvent<Fixture::Touched>(\"Fixture.Touched\")", StringComparison.Ordinal) &&
                    registration.Contains("Host::MakeSharedStruct<Fixture::Hit>(\"Fixture.Bindings.Hit\")", StringComparison.Ordinal),
                    $"Components and events should be registered in the registered namespace, and shared structs by their C# " +
                    $"name, but the registration is:\n{registration}");
        Assert.True(body.Contains("[NativeComponent(\"Fixture.Body\")]", StringComparison.Ordinal) &&
                    touched.Contains("[NativeEvent(\"Fixture.Touched\")]", StringComparison.Ordinal) &&
                    body.Contains("namespace Fixture.Bindings;", StringComparison.Ordinal),
                    $"The attributes should name the registered names, in mirrors of the C# namespace, but Body is:\n{body}");
        Assert.True(mappings.Contains("typeName = \"Fixture.Bindings.Body\"", StringComparison.Ordinal),
                    $"A mapping names the C# type, whatever it is registered as, but the mappings are:\n{mappings}");
    }

    [Fact]
    public void RunningTwiceRewritesNothing()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "");
        var description = WriteDescription(directory, _ => { });

        var first = Generator.Write(config, Generator.Generate(config, [description]));
        var second = Generator.Write(config, Generator.Generate(config, [description]));

        Assert.True(first.Count == 26, $"The first run should write every file, but reported:\n{string.Join('\n', first)}");
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
        File.WriteAllText(Path.Combine(contract, "SphereShape.cs"),
                          "namespace Fixture.Bindings;\n\npublic partial struct SphereShape\n{\n    public float Extra;\n" +
                          "    public int Count { get; set; }\n    public static int Made;\n    public float Total => Extra;\n}\n");

        var exception = Assert.Throws<BindingException>(() => Generator.Generate(config, [description]));

        Assert.True(exception.Message.Contains("SphereShape.cs:5: error: Fixture.Bindings.SphereShape declares the field 'Extra' by hand",
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
                "Fixture::SphereShape::sphereShape: C# cannot name a field SphereShape inside SphereShape; give it another name in [names]")]
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
                    Field(root, "Fixture::SphereShape", "radius")["name"] = "sphereShape";
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
    [InlineData("ByValue", "Fixture.natives.json: Find::origin: cannot map the C++ type 'Fixture::Hit' to C#: a struct " +
                           "crosses by value only as a wrapper (one field of a number, bool, char or enum, or a bound type " +
                           "whose [bound-types] entry names its wrapper kind): take a const pointer")]
    [InlineData("UndescribedByValue", "Fixture.natives.json: Find::origin: cannot map the C++ type 'Fixture::Unknown' to " +
                                      "C#: no description describes Fixture::Unknown, and [natives] types does not name it")]
    [InlineData("TextResult", "Fixture.natives.json: Sum (result): cannot map the C++ type 'std::string_view' to C#: a " +
                              "native returns a number, a bool, an enum, a wrapper, nothing or a void*")]
    [InlineData("Undescribed", "Fixture.natives.json: Find::origin: cannot map the C++ type 'const Fixture::Unknown*' to " +
                               "C#: no description describes Fixture::Unknown, and [natives] types does not name it")]
    [InlineData("NeitherOutNorSpan", "Fixture.natives.json: Find::hit: cannot map the C++ type 'Fixture::Hit*' to C#: a " +
                                     "pointer to non-const data is an output (its doc says [out]), read and " +
                                     "written (its doc says [in,out]) or a span (its std::int32_t count follows it)")]
    [InlineData("Result", "Fixture.natives.json: Sum (result): cannot map the C++ type 'const Fixture::Vec3*' to C#: a " +
                          "native returns a number, a bool, an enum, a wrapper, nothing or a void*")]
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
                        """{ "name": "origin", "type": "Fixture::Hit", "kind": "struct" }"""));
                    break;
                case "UndescribedByValue":
                    Parameter(root, "Find", "origin").ReplaceWith(JsonNode.Parse(
                        """{ "name": "origin", "type": "Fixture::Unknown", "kind": "enum" }"""));
                    break;
                case "TextResult":
                    Native(root, "Sum")["returns"] = JsonNode.Parse("""{ "type": "std::string_view", "kind": "string" }""");
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
                          .Concat(Directory.EnumerateFiles(Path.Combine(FixtureDirectory, "Uses", "Contract"), "*.cs", SearchOption.AllDirectories))
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

        // No node reuse: a reused MSBuild node outlives the build holding its redirected output open, and the reads below
        // would wait for it.
        var start = new ProcessStartInfo("dotnet", ["build", project, "--nologo", "-v:q", "-warnaserror", "--no-incremental",
                                                    "-nodeReuse:false"])
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

    [Fact]
    public void AResourceIsMirroredRegisteredAndTestedInEachScopeAndReadOnly()
    {
        var generated = Generator.Generate(FixtureConfig, [FixtureDescription]);

        var weather = Mirror(generated, "Weather.cs");
        var clock = Mirror(generated, "Clock.cs");
        var sky = Mirror(generated, "Sky.cs");
        Assert.True(weather.Contains("[NativeResource(\"Fixture.Bindings.Weather\")]\npublic partial struct Weather : IResource\n", StringComparison.Ordinal) &&
                    clock.Contains("[NativeResource(\"Fixture.Bindings.Clock\")]\npublic readonly partial struct Clock : IReadOnlyResource\n", StringComparison.Ordinal) &&
                    sky.Contains("[NativeResource(\"Fixture.Bindings.Sky\")]\n[TaggedUnion]\n", StringComparison.Ordinal) &&
                    sky.Contains("public partial struct Sky : IResource\n", StringComparison.Ordinal),
                    "A scene resource, a read-only app resource and a tagged-union app resource should each carry the " +
                    $"resource attribute and their interface, but are:\n{weather}\n{clock}\n{sky}");
        var registration = Output(generated, "FixtureRegistration.cpp");
        Assert.True(registration.Contains("Host::MakeSceneResource<Fixture::Weather>(\"Fixture.Bindings.Weather\")", StringComparison.Ordinal) &&
                    registration.Contains("Host::MakeReadOnlyAppResource<Fixture::Clock>(\"Fixture.Bindings.Clock\")", StringComparison.Ordinal) &&
                    registration.Contains("Host::MakeAppResource<Fixture::Sky>(\"Fixture.Bindings.Sky\")", StringComparison.Ordinal),
                    $"Each resource should be in extension.resources, made by its scope's factory, but the registration is:\n{registration}");
        var defaults = Output(generated, "FixtureDefaultsTests.cpp");
        Assert.True(new[] { "Weather", "Clock", "Sky" }.All(name => defaults.Contains($"CheckDefaults<Fixture::{name}>", StringComparison.Ordinal)),
                    $"The defaults test should have a case per resource, but is:\n{defaults}");
    }

    [Fact]
    public void AnInternalStructIsMirroredInternalWithNoDefaultsCase()
    {
        var generated = Generator.Generate(FixtureConfig, [FixtureDescription]);

        var tag = Mirror(generated, "Tag.cs");
        var defaults = Output(generated, "FixtureDefaults.cs");
        Assert.True(tag.Contains("internal unsafe partial struct Tag\n", StringComparison.Ordinal),
                    $"Tag is in internal-structs, so its mirror should be internal, but is:\n{tag}");
        Assert.False(defaults.Contains(" Tag()", StringComparison.Ordinal),
                     $"A public test method cannot return an internal mirror, so Tag should have no defaults case:\n{defaults}");
    }

    [Fact]
    public void AByHandTypeIsMirroredMappedAndTestedButNotInTheGeneratedRegistration()
    {
        var generated = Generator.Generate(FixtureConfig, [FixtureDescription]);

        Assert.True(Mirror(generated, "BodyId.cs").Contains("public partial struct BodyId", StringComparison.Ordinal) &&
                    Output(generated, "FixtureMappings.hpp").Contains("struct Iris::StructMapping<Fixture::BodyId>", StringComparison.Ordinal) &&
                    Output(generated, "FixtureDefaultsTests.cpp").Contains("CheckDefaults<Fixture::BodyId>", StringComparison.Ordinal),
                    "BodyId is in by-hand, so it should still get its mirror, mapping and defaults case.");
        var registration = Output(generated, "FixtureRegistration.cpp");
        Assert.False(registration.Contains("<Fixture::BodyId>", StringComparison.Ordinal),
                     $"BodyId is in by-hand, so the generated registration should not make it, but is:\n{registration}");
    }

    [Fact]
    public void ATypeABindingItUsesMirrorsIsNamedThereAndNotGeneratedHere()
    {
        var generated = Generator.Generate(FixtureConfig, [FixtureDescription]);

        Assert.False(generated.Keys.Any(path => Path.GetFileName(path) == "Spring.cs"),
                     "Spring is the used Common binding's, so this one should give it no mirror.");
        var body = Mirror(generated, "Body.cs");
        Assert.True(body.Contains("using Fixture.Common;\n", StringComparison.Ordinal) &&
                    body.Contains("    public Spring Tuned = new() { Stiffness = 4 };\n", StringComparison.Ordinal),
                    $"Body should name the Common binding's Spring, members by its names, but is:\n{body}");
        var mappings = Output(generated, "FixtureMappings.hpp");
        Assert.True(mappings.Contains("#include \"../../Uses/Native/Generated/CommonMappings.hpp\"\n", StringComparison.Ordinal) &&
                    !mappings.Contains("StructMapping<Fixture::Spring>", StringComparison.Ordinal),
                    $"The mappings should include the Common binding's and map Spring no more, but are:\n{mappings}");
        Assert.False(generated.Values.Any(text => text.Contains("Fixture.Bindings.Spring", StringComparison.Ordinal)),
                     "No output should name a Spring of this binding's namespace.");
    }

    [Fact]
    public void NativesTakeTextEnumsAndWrappersByValue()
    {
        var natives = Mirror(Generator.Generate(FixtureConfig, [FixtureDescription]), "FixtureNative.cs");

        string[] expected =
        [
            "using System;\n",
            "    public static partial void Show(ReadOnlySpan<char> text, uint glyph);\n",
            "    public static partial BodyId Advance(BodyId body, Motion motion);\n",
            "    public static partial Motion MotionOf(BodyId body);\n",
            "    public static partial Ticket Renew(Ticket ticket);\n",
            "    public static partial void Turn(in Quaternion rotation, int turns);\n",
        ];
        foreach (var line in expected)
        {
            Assert.True(natives.Contains(line, StringComparison.Ordinal),
                        $"The natives' class should declare\n  {line}but is:\n{natives}");
        }
    }

    [Theory]
    [InlineData("UsedAndExcluded", "Fixture::Spring is mirrored by the binding it uses, ", "but exclude names it too")]
    [InlineData("UsedAndBound", "Fixture::Spring is mirrored by the binding it uses, ", "but [bound-types] names it too")]
    [InlineData("UsedAndSelected", "Fixture::Spring is mirrored by the binding it uses, ",
                "but Fixture.api.json selects it as a component and Common.api.json as a shared struct: select it as one")]
    [InlineData("UsedWithAnotherLayout", "Fixture::Spring is mirrored by the binding it uses, ", "which describes it with another layout")]
    [InlineData("UsedTwice", "Fixture::Spring is mirrored by the binding it uses, ", "use the type from one of them")]
    [InlineData("UsesMissing", "uses ", "Missing.binding.toml, which does not exist")]
    [InlineData("UsesWithoutDescriptions", "uses ", "which names no descriptions: list the descriptions it binds in its 'descriptions'")]
    [InlineData("UsesItself", "Other.binding.toml: uses itself, through ", "")]
    [InlineData("DescriptionsDisagree", "'descriptions' names ", "name the same descriptions")]
    [InlineData("NoResourceAttribute", "resource, but [csharp] has no resource-attribute", "")]
    [InlineData("NoReadOnlyResourceInterface",
                "Fixture::Clock is a read-only app resource, but [csharp] has no read-only-resource-interface", "")]
    [InlineData("NoResourceFactory", "Fixture::Weather is a scene resource, but [registration] has no make-scene-resource", "")]
    [InlineData("ReadOnlyNotRegistrable", "[registration] read-only names Fixture::Hit, which is not a described component or resource", "")]
    [InlineData("ByHandNotRegistrable",
                "[registration] by-hand names Fixture::Numbers, which is not a described component, event, resource or " +
                "shared struct this binding mirrors", "")]
    [InlineData("InternalHeld", "internal-structs names Fixture::Limits, but the public Fixture::Body holds one: list it too", "")]
    [InlineData("InternalNotStruct", "internal-structs names Fixture::Motion, which is not a described struct this binding mirrors", "")]
    [InlineData("WrapperNotNumber", "[bound-types] Fixture::Ticket's wrapper is 'struct', which is not a number kind", "")]
    public void WhatTheConfigurationCannotHonourFailsSayingWhy(string problem, string diagnostic, string rest)
    {
        using var directory = new TemporaryDirectory();
        // A copy of the used binding's configuration, edited, in the directory.
        string Other(Func<string, string> edit)
        {
            var path = Path.Combine(directory.Path, "Other.binding.toml");
            var text = System.IO.File.ReadAllText(UsesConfig)
                           .Replace("\"Common.api.json\"", $"\"{Path.Combine(FixtureDirectory, "Uses", "Common.api.json").Replace('\\', '/')}\"",
                                    StringComparison.Ordinal);
            System.IO.File.WriteAllText(path, edit(text));
            return path.Replace('\\', '/');
        }

        string Uses(string text, string used) => text.Replace("uses = [", $"uses = [\"{used}\", ", StringComparison.Ordinal);

        Func<string, string> edit = problem switch
        {
            "UsedAndExcluded" => text => text.Replace("[csharp]", "exclude = [\"Fixture::Spring\"]\n\n[csharp]", StringComparison.Ordinal),
            "UsedAndBound" => text => text.Replace("[bound-types]\n", "[bound-types]\n\"Fixture::Spring\" = { type = \"Spring\" }\n", StringComparison.Ordinal),
            "UsedTwice" => text => Uses(text, Other(other => other.Replace("Fixture.Common", "Fixture.Other", StringComparison.Ordinal))),
            "UsesMissing" => text => Uses(text, "Missing.binding.toml"),
            "UsesWithoutDescriptions" => text => Uses(text, Other(other => other.Replace("descriptions = [", "# [", StringComparison.Ordinal))),
            "UsesItself" => text => Uses(text, Other(other => "uses = [\"Other.binding.toml\"]\n" + other)),
            "DescriptionsDisagree" => text => text.Replace("[csharp]", "descriptions = [\"Elsewhere.api.json\"]\n\n[csharp]", StringComparison.Ordinal),
            "NoResourceAttribute" => text => text.Replace("resource-attribute = \"NativeResource\"\n", "", StringComparison.Ordinal),
            "NoReadOnlyResourceInterface" => text => text.Replace("read-only-resource-interface = \"IReadOnlyResource\"\n", "", StringComparison.Ordinal),
            "NoResourceFactory" => text => text.Replace("make-scene-resource = \"Host::MakeSceneResource\"\n", "", StringComparison.Ordinal),
            "ReadOnlyNotRegistrable" => text => text.Replace("read-only = [\"Fixture::Collider\"", "read-only = [\"Fixture::Hit\"", StringComparison.Ordinal),
            "ByHandNotRegistrable" => text => text.Replace("by-hand = [\"Fixture::BodyId\"]", "by-hand = [\"Fixture::Numbers\"]", StringComparison.Ordinal),
            "InternalHeld" => text => text.Replace("internal-structs = [\"Fixture::Tag\"]", "internal-structs = [\"Fixture::Tag\", \"Fixture::Limits\"]", StringComparison.Ordinal),
            "InternalNotStruct" => text => text.Replace("internal-structs = [\"Fixture::Tag\"]", "internal-structs = [\"Fixture::Motion\"]", StringComparison.Ordinal),
            "WrapperNotNumber" => text => text.Replace("wrapper = \"u32\"", "wrapper = \"struct\"", StringComparison.Ordinal),
            _ => text => text,
        };
        var config = CopyConfig(directory, "", edit: edit);
        var description = WriteDescription(directory, root =>
        {
            switch (problem)
            {
                case "UsedAndSelected":
                    Type(root, "Fixture::Spring")["role"] = "component";
                    break;
                case "UsedWithAnotherLayout":
                    Type(root, "Fixture::Spring")["alignment"] = 8;
                    break;
            }
        });

        var exception = Assert.Throws<BindingException>(() => Generator.Generate(config, [description]));

        Assert.True(exception.Message.Contains(diagnostic, StringComparison.Ordinal) && exception.Message.Contains(rest, StringComparison.Ordinal),
                    $"Binding should fail with\n  {diagnostic}...{rest}\nbut failed with\n  {exception.Message}");
    }

    [Theory]
    [InlineData("Alike", "")]
    [InlineData("Differently", "Fixture::Hit is also described by")]
    [InlineData("TwoRoles", "with another role; a type described twice is selected in one description at most")]
    public void ATypeTwoDescriptionsDescribeIsKeptOnceWhenAlike(string problem, string diagnostic)
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "");
        var description = WriteDescription(directory, _ => { });
        // A second description holding Hit as a binding's own description holds a package's type: nested, its own doc.
        var root = JsonNode.Parse(System.IO.File.ReadAllText(FixtureDescription))!;
        var hit = Type(root, "Fixture::Hit").DeepClone();
        hit["role"] = problem == "TwoRoles" ? "component" : "nested";
        hit["doc"] = "Described again.";
        if (problem == "Differently")
        {
            hit["fields"]![2]!["offset"] = 32;
        }

        var second = Path.Combine(directory.Path, "Second.api.json");
        System.IO.File.WriteAllText(second, new JsonObject
        {
            ["package"] = "Second", ["pointerSize"] = 8, ["alignment"] = "natural", ["types"] = new JsonArray(hit),
        }.ToJsonString());

        if (problem != "Alike")
        {
            var exception = Assert.Throws<BindingException>(() => Generator.Generate(config, [description, second]));
            Assert.True(exception.Message.Contains(diagnostic, StringComparison.Ordinal),
                        $"Binding should fail with\n  {diagnostic}\nbut failed with\n  {exception.Message}");
            return;
        }

        var once = Generator.Generate(config, [description]);
        var twice = Generator.Generate(config, [description, second]);
        Assert.True(Changes(once, twice).Count == 0,
                    "A type a second description describes alike should be bound once, as the first selects it, but " +
                    $"the outputs changed:\n{string.Join('\n', Changes(once, twice))}");
    }

    [Fact]
    public void ABindingWhoseNativesAreNotDescribedYetBindsNone()
    {
        using var directory = new TemporaryDirectory();
        var config = CopyConfig(directory, "");
        var description = WriteDescription(directory, _ => { });
        System.IO.File.Delete(Path.Combine(directory.Path, "Fixture.natives.json"));

        var generated = Generator.Generate(config, [description]);

        var natives = Mirror(generated, "FixtureNative.cs");
        var registration = Output(generated, "FixtureRegistration.cpp");
        Assert.False(natives.Contains("[NativeFunction]", StringComparison.Ordinal),
                     $"With no natives description yet, the natives' class should declare none, but is:\n{natives}");
        Assert.True(registration.Contains("extension.functions = {};", StringComparison.Ordinal),
                    $"With no natives description yet, the registration should list no natives, but is:\n{registration}");
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

        // The binding it uses stays where it is: only this binding's outputs land in the directory.
        text = text.Replace("\"Uses/Common.binding.toml\"", $"\"{UsesConfig.Replace('\\', '/')}\"", StringComparison.Ordinal);
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
        Output(generated, fileName);

    // The generated file of that name.
    private static string Output(IReadOnlyDictionary<string, string> generated, string fileName) =>
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
