using System.Runtime.CompilerServices;
using Xunit;

namespace DescribeApi.Tests;

public sealed class DescribeTests
{
    private static string FixtureDirectory([CallerFilePath] string here = "") =>
        Path.Combine(Path.GetDirectoryName(here)!, "Fixture");

    private static string FixtureSelection => Path.Combine(FixtureDirectory(), "Fixture.api.toml");

    [Fact]
    public void TheFixtureGivesTheCommittedDescriptionByteForByte()
    {
        var selection = Selection.Read(FixtureSelection);
        var described = DescriptionWriter.Write(Describer.Describe(selection, []));
        var committed = File.ReadAllText(selection.DescriptionPath);

        Assert.True(committed == described,
                    "The fixture's description differs from the committed Fixture.api.json. If the change is meant, " +
                    "regenerate it (the fenrir-describe-api target) and review the diff.\n" + FirstDifference(committed, described));
    }

    [Fact]
    public void RunningTwiceOnUnchangedHeadersRewritesNothing()
    {
        using var copy = new TemporaryDirectory();
        File.Copy(Path.Combine(FixtureDirectory(), "Fixture.hpp"), Path.Combine(copy.Path, "Fixture.hpp"));
        var selection = Path.Combine(copy.Path, "Fixture.api.toml");
        File.Copy(FixtureSelection, selection);
        var description = Path.Combine(copy.Path, "Fixture.api.json");

        Assert.True(Program.Main([selection]) == 0, "The first run should describe the fixture.");
        var firstWrite = File.GetLastWriteTimeUtc(description);
        File.SetLastWriteTimeUtc(description, firstWrite.AddHours(-1));
        Assert.True(Program.Main([selection]) == 0, "The second run should describe the fixture.");

        Assert.True(File.GetLastWriteTimeUtc(description) == firstWrite.AddHours(-1),
                    "A second run on unchanged headers rewrote the description.");
    }

    [Theory]
    [InlineData("UnknownDefault",
                "Failing::UnknownDefault::scale: the default 'Scale()' is not one the description records")]
    [InlineData("PrivateMember",
                "Failing::PrivateMember::m_hidden: a private data member outside the tagged-union shape " +
                "(one enum tag followed by one anonymous union)")]
    [InlineData("ReferenceMember",
                "Failing::ReferenceMember::target: a reference member: a shared type holds values, not references")]
    [InlineData("VirtualFunction",
                "Failing::VirtualFunction::Update: a virtual function: a shared type is plain data")]
    [InlineData("BaseWithData",
                "Failing::BaseWithData: its base class Base has data members or virtual functions")]
    [InlineData("NonTriviallyCopyableMember",
                "Failing::Copied: not trivially copyable: it declares its own copy, move, assignment or destructor " +
                "(reached through Failing::NonTriviallyCopyableMember::member)")]
    [InlineData("Inexpressible",
                "Failing::Inexpressible::wide: its type 'long double' is not a kind the description records")]
    [InlineData("NoInitialiser", "Failing::NoInitialiser::value: has no default member initialiser")]
    [InlineData("UnnamedArguments",
                "Failing::UnnamedArguments::pair: cannot tell which component of Failing::Pair each argument sets: " +
                "the constructor's parameters (a, b) are not named after its components (first, second)")]
    public void EachUndescribableTypeFailsNamingTheTypeAndMember(string type, string diagnostic)
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, $"{type}.api.toml");
        var boundPair = type == "UnnamedArguments" ? "\"Failing::Pair\" = \"bound-type\"\n" : "";
        File.WriteAllText(selection,
                          $"headers = [\"{header}\"]\n\n[types]\n\"Failing::{type}\" = \"component\"\n{boundPair}");

        var exception = Assert.Throws<DescribeException>(() => Describer.Describe(Selection.Read(selection), []));

        Assert.True(exception.Message.Contains("error: " + diagnostic, StringComparison.Ordinal),
                    $"Describing Failing::{type} should fail with\n  {diagnostic}\nbut failed with\n  {exception.Message}");
        Assert.False(File.Exists(Path.Combine(directory.Path, $"{type}.api.json")),
                     "A failed run should write no description.");
    }

    private static string FirstDifference(string expected, string actual)
    {
        var expectedLines = expected.Split('\n');
        var actualLines = actual.Split('\n');
        for (var i = 0; i < Math.Max(expectedLines.Length, actualLines.Length); ++i)
        {
            var e = i < expectedLines.Length ? expectedLines[i] : "<end>";
            var a = i < actualLines.Length ? actualLines[i] : "<end>";
            if (e != a)
            {
                return $"First difference at line {i + 1}:\n  committed: {e}\n  described: {a}";
            }
        }

        return "The texts differ only in line endings or encoding.";
    }

    private sealed class TemporaryDirectory : IDisposable
    {
        public TemporaryDirectory()
        {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "describe-api-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public void Dispose() => Directory.Delete(Path, recursive: true);
    }
}
