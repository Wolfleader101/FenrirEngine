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
                "(one enum tag followed by one anonymous union); a selection shares one only when 'private-members' " +
                "names its type")]
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

    [Theory]
    [InlineData("scene-resource")]
    [InlineData("app-resource")]
    public void AResourceRoleOnAnEnumFails(string role)
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "EnumRole.api.toml");
        File.WriteAllText(selection, $"headers = [\"{header}\"]\n\n[types]\n\"Failing::Mode\" = \"{role}\"\n");

        var exception = Assert.Throws<DescribeException>(() => Describer.Describe(Selection.Read(selection), []));

        Assert.True(exception.Message.Contains("error: Failing::Mode: an enum takes no role; it is described when a field uses " +
                                               "it, or when the selection names it as an enum", StringComparison.Ordinal),
                    $"An enum selected as a {role} should fail naming it, but failed with\n  {exception.Message}");
    }

    [Fact]
    public void ATypeThatIsNotAnEnumSelectedAsAnEnumFails()
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "NotEnum.api.toml");
        File.WriteAllText(selection, $"headers = [\"{header}\"]\n\n[types]\n\"Failing::NoInitialiser\" = \"enum\"\n");

        var exception = Assert.Throws<DescribeException>(() => Describer.Describe(Selection.Read(selection), []));

        Assert.True(exception.Message.Contains("error: Failing::NoInitialiser: not an enum, so it cannot be selected as one",
                                               StringComparison.Ordinal),
                    $"A struct selected as an enum should fail naming it, but failed with\n  {exception.Message}");
    }

    [Theory]
    [InlineData("\"Failing::NoInitialiser\" = \"component\"")]
    [InlineData("\"Failing::PrivateMember\" = \"bound-type\"")]
    public void PrivateMembersNamingATypeTheSelectionDoesNotShareFails(string types)
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "Private.api.toml");
        File.WriteAllText(selection,
                          $"headers = [\"{header}\"]\nprivate-members = [\"Failing::PrivateMember\"]\n\n[types]\n{types}\n");

        var exception = Assert.Throws<DescribeException>(() => Selection.Read(selection));

        Assert.True(exception.Message.Contains("'private-members' names Failing::PrivateMember, which [types] does not " +
                                               "select as a component, event, resource or shared struct", StringComparison.Ordinal),
                    "A private-members entry that is not a selected plain-data type should fail naming it, but failed " +
                    $"with\n  {exception.Message}");
    }

    [Fact]
    public void TheFixturesNativesGiveTheCommittedNativesDescriptionByteForByte()
    {
        var selection = NativesSelection.Read(Path.Combine(FixtureDirectory(), "Fixture.binding.toml"));
        var described = NativesWriter.Write(NativesDescriber.Describe(selection, []));
        var committed = File.ReadAllText(selection.DescriptionPath);

        Assert.True(committed == described,
                    "The fixture's natives description differs from the committed Fixture.natives.json. If the change is " +
                    "meant, regenerate it (the fenrir-describe-api target) and review the diff.\n" +
                    FirstDifference(committed, described));
    }

    [Theory]
    [InlineData("Hidden: has internal linkage (static, or in an anonymous namespace): the generated registration " +
                "declares and names it from another source file")]
    [InlineData("ByReference::value: is a reference: after the call context and the extension state, a native takes " +
                "values and pointers")]
    [InlineData("Unnamed (parameter 2): is unnamed: a native's parameters are named in C#")]
    [InlineData("OutValue::value: is documented @param[out] but is not a pointer to non-const data")]
    [InlineData("InOutConst::values: is documented @param[in,out] but is not a pointer to non-const data")]
    [InlineData("NotRunningVoid: documents @notrunning but returns nothing")]
    [InlineData("DocumentsMissing: documents a parameter 'missing' it does not have")]
    [InlineData("PointerToPointer::values: is a pointer to a pointer, which the description cannot record")]
    [InlineData("Wide::wide: its type 'long double' is not a kind the description records")]
    [InlineData("TextByPointer::text: is a pointer to a std::string_view: a native takes text as a std::string_view by value")]
    public void EachUndescribableNativeFailsNamingTheFunctionAndParameter(string diagnostic)
    {
        using var directory = new TemporaryDirectory();
        var source = Path.Combine(FixtureDirectory(), "FailingNatives.cpp").Replace('\\', '/');
        var config = Path.Combine(directory.Path, "Failing.binding.toml");
        File.WriteAllText(config, $"[natives]\nsources = [\"{source}\"]\ncontext = \"Host::CallContext\"\n");

        var exception = Assert.Throws<DescribeException>(() => NativesDescriber.Describe(NativesSelection.Read(config), []));

        Assert.True(exception.Message.Contains("error: " + diagnostic, StringComparison.Ordinal),
                    $"Describing FailingNatives.cpp should report\n  {diagnostic}\namong\n  {exception.Message}");
        Assert.False(File.Exists(Path.Combine(directory.Path, "Failing.natives.json")),
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
