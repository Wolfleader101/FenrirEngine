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
                "Failing::VirtualFunction::Update: a virtual function: a component is plain data")]
    [InlineData("BaseWithData",
                "Failing::BaseWithData: its base class Base has data members or virtual functions, which a component's " +
                "base may not have (only a data asset extends a type with data members)")]
    [InlineData("NonTriviallyCopyableMember",
                "Failing::Copied: not trivially copyable: it declares its own copy, move, assignment or destructor, " +
                "which a nested type may not (only a data asset or settings type may) " +
                "(reached through Failing::NonTriviallyCopyableMember::member)")]
    [InlineData("Inexpressible",
                "Failing::Inexpressible::wide: its type 'long double' is not a kind the description records")]
    [InlineData("NoInitialiser", "Failing::NoInitialiser::value: has no default member initialiser")]
    [InlineData("UnnamedArguments",
                "Failing::UnnamedArguments::pair: cannot tell which component of Failing::Pair each argument sets: " +
                "the constructor's parameters (a, b) are not named after its components (first, second)")]
    [InlineData("MalformedRange",
                "Failing::MalformedRange::speed: @range takes two numbers, a minimum and a maximum not below it: '@range 10'")]
    [InlineData("VersionZero", "Failing::VersionZero: @version takes a whole number from 1: '@version 0'")]
    [InlineData("FieldVersion", "Failing::FieldVersion::value: @version is a type's doc command, not a field's")]
    [InlineData("TypeRange", "Failing::TypeRange: @range is a field's doc command, not a type's")]
    [InlineData("FormerlyWithoutName",
                "Failing::FormerlyWithoutName::value: @formerly takes the field's former name: '@formerly'")]
    [InlineData("UnknownCommand",
                "Failing::UnknownCommand::value: @colour is not a doc command (a type takes @version, @formerly, " +
                "@transient and @editor-only; a field @formerly, @range, @step, @display, @group, @readonly, @hidden, " +
                "@multiline, @units and @transient)")]
    [InlineData("TransientTag",
                "Failing::TransientTag::kind: @transient on a tagged union's tag, which the union cannot leave out")]
    [InlineData("EnumeratorCommand",
                "Failing::Tinted::Red: @hidden: an enumerator takes no doc command " +
                "(reached through Failing::EnumeratorCommand::tint)")]
    [InlineData("UnevaluatedDefault",
                "Failing::UnevaluatedDefault::count: the default 'Count() * 2' is not one the description records")]
    [InlineData("NamedEntity",
                "Failing::NamedEntity::owner: the default '{7, nullptr}' is not one an entity records: its default " +
                "is the empty entity (no initialiser, {} or Entity())")]
    [InlineData("NamedAsset",
                "Failing::NamedAsset::mesh: the default '{1, 1}' is not one a reference records: its default is the " +
                "empty handle (no initialiser, {} or AssetHandle<T>())")]
    [InlineData("ComputedText",
                "Failing::ComputedText::label: the default '{static_cast<char>(Count())}' is not one a fixed string " +
                "records: a string literal, a braced list of character constants, or {}")]
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
    [InlineData("\"Failing::NamedComponent\" = \"component\"",
                "Failing::NamedComponent::name: a string, which a component cannot hold: the owning kinds (string, list, " +
                "optional and path) are for data assets and settings types")]
    [InlineData("\"Failing::ListedEvent\" = \"event\"", "Failing::ListedEvent::values: a list, which an event cannot hold")]
    [InlineData("\"Failing::MaybeResource\" = \"scene-resource\"",
                "Failing::MaybeResource::value: an optional, which a scene resource cannot hold")]
    [InlineData("\"Failing::MaybeResource\" = \"app-resource\"",
                "Failing::MaybeResource::value: an optional, which an app resource cannot hold")]
    [InlineData("\"Failing::RootedShared\" = \"shared-struct\"",
                "Failing::RootedShared::root: a path, which a shared struct cannot hold")]
    [InlineData("\"Failing::HoldsLabelledOnly\" = \"component\"",
                "Failing::Labelled::label: a string, which a nested type reached from a component cannot hold: the " +
                "owning kinds (string, list, optional and path) are for data assets and settings types (reached through " +
                "Failing::HoldsLabelledOnly::labelled)")]
    [InlineData("\"Failing::HoldsLabelled\" = \"component\"\n\"Failing::AssetHoldsLabelled\" = \"data-asset\"",
                "Failing::Labelled: holds a string (Failing::Labelled::label), which a component cannot reach, but is " +
                "reached from a component through Failing::HoldsLabelled::labelled and from a data asset through " +
                "Failing::AssetHoldsLabelled::labelled")]
    [InlineData("\"Failing::NamedAssetBase\" = \"data-asset\"\n\"Failing::DerivedAsset\" = \"data-asset\"\n" +
                "\"Failing::HoldsDerivedAsset\" = \"component\"",
                "Failing::NamedAssetBase: holds a string (Failing::NamedAssetBase::name), which a component cannot " +
                "reach, but is reached from a component through Failing::HoldsDerivedAsset::held and from a data asset " +
                "where it is selected")]
    [InlineData("\"Failing::VirtualAsset\" = \"data-asset\"",
                "Failing::VirtualAsset::~VirtualAsset: a virtual function: a data asset is plain data")]
    [InlineData("\"Failing::SharedBase\" = \"shared-struct\"\n\"Failing::ExtendsShared\" = \"data-asset\"",
                "Failing::ExtendsShared: its base class Failing::SharedBase is a shared struct: a data asset extends only " +
                "another data asset")]
    [InlineData("\"Failing::ExtendsShared\" = \"data-asset\"",
                "Failing::ExtendsShared: its base class Failing::SharedBase is not selected: a data asset extends only " +
                "another data asset")]
    [InlineData("\"Failing::AssetA\" = \"data-asset\"\n\"Failing::AssetB\" = \"data-asset\"\n\"Failing::ExtendsTwo\" = \"data-asset\"",
                "Failing::ExtendsTwo: extends 2 types: a data asset extends one other data asset at most")]
    [InlineData("\"Failing::ExtendsSettings\" = \"settings\"",
                "Failing::ExtendsSettings: its base class SettingsBase has data members or virtual functions, which a " +
                "settings type's base may not have (only a data asset extends a type with data members)")]
    [InlineData("\"Failing::DoublyOptional\" = \"data-asset\"",
                "Failing::DoublyOptional::value: an optional of an optional, which the kinds cannot express")]
    [InlineData("\"Failing::Flags\" = \"settings\"",
                "Failing::Flags::flags: a std::vector<bool>, whose elements are bits rather than values: hold a list of u8")]
    [InlineData("\"Failing::FilledList\" = \"data-asset\"",
                "Failing::FilledList::values: the default '{1.0f, 2.0f}' is not one a list records: its default is the " +
                "empty list (no initialiser or {})")]
    [InlineData("\"Failing::FilledOptional\" = \"data-asset\"",
                "Failing::FilledOptional::value: the default '1.0f' is not one an optional records: its default is " +
                "empty (no initialiser, {} or std::nullopt)")]
    [InlineData("\"Failing::ComputedName\" = \"data-asset\"",
                "Failing::ComputedName::name: the default 'std::string(3, 'a')' is not one a string records: a string " +
                "literal, or {} or no initialiser for the empty string")]
    [InlineData("\"Failing::TransientField\" = \"component\"",
                "Failing::TransientField::value: @transient on a field, which a component cannot leave out: a field " +
                "marked @transient is code wiring a data asset or settings type holds, and a component's fields are its " +
                "layout")]
    [InlineData("\"Failing::ArrayOfStrings\" = \"data-asset\"",
                "Failing::ArrayOfStrings::names: an array of strings, which the kinds cannot express: hold a list")]
    public void EachRoleRefusesWhatItCannotHoldNamingTheTypeMemberAndRole(string types, string diagnostic)
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "Roles.api.toml");
        File.WriteAllText(selection, $"headers = [\"{header}\"]\n\n[types]\n{types}\n");

        var exception = Assert.Throws<DescribeException>(() => Describer.Describe(Selection.Read(selection), []));

        Assert.True(exception.Message.Contains("error: " + diagnostic, StringComparison.Ordinal),
                    $"Describing\n  {types}\nshould fail with\n  {diagnostic}\nbut failed with\n  {exception.Message}");
    }

    [Fact]
    public void ADataAssetRecordsTheDataAssetItExtendsAndASettingsTypeNone()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        StructDescription Of(string name) => (StructDescription)types.Single(type => type.Name == name);

        Assert.Equal((Role.DataAsset, (string?)null), (Of("Fixture::WeaponData").Role, Of("Fixture::WeaponData").Base));
        Assert.Equal((Role.DataAsset, (string?)"Fixture::WeaponData"), (Of("Fixture::Sword").Role, Of("Fixture::Sword").Base));
        Assert.Equal((Role.Settings, (string?)null), (Of("Fixture::Paths").Role, Of("Fixture::Paths").Base));
        Assert.Equal(Role.Nested, Of("Fixture::Stat").Role);
    }

    [Fact]
    public void ADataAssetRecordsOnlyItsOwnFieldsAfterItsBase()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        var names = types.Select(type => type.Name).ToList();
        var sword = (StructDescription)types.Single(type => type.Name == "Fixture::Sword");

        Assert.Equal(["reach", "combos", "titles"], sword.Fields.Select(field => field.Name));
        Assert.True(names.IndexOf("Fixture::WeaponData") < names.IndexOf("Fixture::Sword"),
                    "A data asset should be described after the base it extends.");
    }

    [Fact]
    public void TheOwningKindsAreRecordedWithTheirElementsAndNoOffset()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        FieldDescription Of(string type, string name) =>
            ((StructDescription)types.Single(t => t.Name == type)).Fields.Single(field => field.Name == name);

        Assert.Equal(new FieldType("string"), Of("Fixture::WeaponData", "displayName").Type);
        Assert.Equal(new FieldType("list", Element: new FieldType("struct", "Fixture::Stat")),
                     Of("Fixture::WeaponData", "stats").Type);
        Assert.Equal(new FieldType("optional", Element: new FieldType("f32")), Of("Fixture::WeaponData", "cooldown").Type);
        Assert.Equal(new FieldType("list", Element: new FieldType("list", Element: new FieldType("i32"))),
                     Of("Fixture::Sword", "combos").Type);
        Assert.Equal(new FieldType("optional", Element: new FieldType("list", Element: new FieldType("string"))),
                     Of("Fixture::Sword", "titles").Type);
        Assert.Equal(new FieldType("path"), Of("Fixture::Paths", "root").Type);
        Assert.Equal(new FieldType("string"), Of("Fixture::Stat", "name").Type);
    }

    [Fact]
    public void ATypeReachedByMemberRecordsNoOffsetsAndAFixedLayoutTypeKeepsThem()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        StructDescription Of(string name) => (StructDescription)types.Single(type => type.Name == name);

        // Its layout is the standard library's, so not even a number beside an owning kind records its offset.
        foreach (var name in new[] { "Fixture::WeaponData", "Fixture::Sword", "Fixture::Paths", "Fixture::Stat" })
        {
            Assert.True(Of(name).ByMember, $"{name} should be reached by member.");
            Assert.All(Of(name).Fields, field => Assert.Null(field.Offset));
        }

        Assert.False(Of("Fixture::Socket").ByMember);
        Assert.All(Of("Fixture::Socket").Fields, field => Assert.NotNull(field.Offset));
    }

    [Fact]
    public void AStringOrPathDefaultIsItsLiteralAndAListOrOptionalDefaultIsEmpty()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        DefaultValue Of(string type, string name) =>
            ((StructDescription)types.Single(t => t.Name == type)).Fields.Single(field => field.Name == name).Default;

        Assert.Equal(new DefaultValue.Text("Weapon"), Of("Fixture::WeaponData", "displayName"));
        Assert.Equal(new DefaultValue.Text(""), Of("Fixture::Stat", "name"));
        Assert.Equal(new DefaultValue.Text(""), Of("Fixture::Paths", "label"));
        Assert.Equal(new DefaultValue.Text("assets"), Of("Fixture::Paths", "root"));
        Assert.Empty(Assert.IsType<DefaultValue.Array>(Of("Fixture::WeaponData", "stats")).Elements);
        Assert.Empty(Assert.IsType<DefaultValue.Array>(Of("Fixture::Sword", "combos")).Elements);
        Assert.Equal(new DefaultValue.Null(), Of("Fixture::WeaponData", "cooldown"));
        Assert.Equal(new DefaultValue.Null(), Of("Fixture::Sword", "titles"));
    }

    [Fact]
    public void ADataAssetMayHoldAComponentTypeByValue()
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "Holds.api.toml");
        // Base has no owning kind, so a component and a data asset may both hold it.
        File.WriteAllText(selection, $"headers = [\"{header}\"]\n\n[types]\n\"Failing::Base\" = \"component\"\n" +
                                     "\"Failing::HoldsBase\" = \"data-asset\"\n");

        var types = Describer.Describe(Selection.Read(selection), []).Types;

        Assert.Contains(types, type => type.Name == "Failing::HoldsBase");
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

    [Fact]
    public void PrivateMembersNamingABoundTypeFails()
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "Private.api.toml");
        File.WriteAllText(selection,
                          $"headers = [\"{header}\"]\nprivate-members = [\"Failing::PrivateMember\"]\n\n[types]\n" +
                          "\"Failing::PrivateMember\" = \"bound-type\"\n");

        var exception = Assert.Throws<DescribeException>(() => Selection.Read(selection));

        Assert.True(exception.Message.Contains("'private-members' names Failing::PrivateMember, which [types] selects as a " +
                                               "bound type or an enum, not a component, event, resource or shared struct",
                                               StringComparison.Ordinal),
                    $"A private-members entry selected as a bound type should fail naming it, but failed with\n  {exception.Message}");
    }

    [Fact]
    public void PrivateMembersNamingATypeNothingSelectedHoldsFails()
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "Private.api.toml");
        File.WriteAllText(selection,
                          $"headers = [\"{header}\"]\nprivate-members = [\"Failing::PrivateMember\"]\n\n[types]\n" +
                          "\"Failing::NoInitialiser\" = \"component\"\n");

        var exception = Assert.Throws<DescribeException>(() => Describer.Describe(Selection.Read(selection), []));

        Assert.True(exception.Message.Contains("error: 'private-members' names Failing::PrivateMember, which [types] does " +
                                               "not select and no selected type holds", StringComparison.Ordinal),
                    "A private-members entry that is neither selected nor held by a selected type should fail naming it, " +
                    $"but failed with\n  {exception.Message}");
    }

    [Fact]
    public void ANestedTypeWithAPrivateMemberIsDescribedOnlyWhenPrivateMembersNamesIt()
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "Holds.api.toml");
        const string types = "[types]\n\"Failing::HoldsPrivateMember\" = \"component\"\n";
        File.WriteAllText(selection, $"headers = [\"{header}\"]\n\n{types}");

        var exception = Assert.Throws<DescribeException>(() => Describer.Describe(Selection.Read(selection), []));
        Assert.True(exception.Message.Contains("error: Failing::PrivateMember::m_hidden: a private data member outside the " +
                                               "tagged-union shape", StringComparison.Ordinal) &&
                    exception.Message.Contains("(reached through Failing::HoldsPrivateMember::member)", StringComparison.Ordinal),
                    "Without private-members, a nested type's private member should fail naming the field it is reached " +
                    $"through, but failed with\n  {exception.Message}");

        File.WriteAllText(selection, $"headers = [\"{header}\"]\nprivate-members = [\"Failing::PrivateMember\"]\n\n{types}");

        var description = Describer.Describe(Selection.Read(selection), []);

        var nested = (StructDescription)description.Types.Single(type => type.Name == "Failing::PrivateMember");
        Assert.True(nested.Role == Role.Nested && nested.Fields.Select(field => field.Name).SequenceEqual(["visible", "m_hidden"]),
                    "With private-members naming it, the nested type should be described with its private member, but is " +
                    $"{nested.Role} with fields {string.Join(", ", nested.Fields.Select(field => field.Name))}");
    }

    [Fact]
    public void TypeDocCommandsAreRecordedAsKeysAndLeaveTheDocText()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        var dial = (StructDescription)types.Single(type => type.Name == "Fixture::Dial");
        var contacts = types.Single(type => type.Name == "Fixture::Contacts");
        var folder = types.Single(type => type.Name == "Fixture::Folder");
        var body = types.Single(type => type.Name == "Fixture::Body");

        Assert.Equal((2, false, false), (dial.Commands.Version, dial.Commands.Transient, dial.Commands.EditorOnly));
        Assert.Equal(["Fixture::Gauge", "Legacy::Meter"], dial.Commands.Formerly);
        Assert.Equal("A component a file reads under its former names, at version 2.\n\nThe doc commands below are " +
                     "recorded as keys, not as text.", dial.Doc);
        Assert.Equal((1, true, false), (contacts.Commands.Version, contacts.Commands.Transient, contacts.Commands.EditorOnly));
        Assert.Equal((1, false, true), (folder.Commands.Version, folder.Commands.Transient, folder.Commands.EditorOnly));
        Assert.Same(TypeCommands.None, body.Commands);
    }

    [Fact]
    public void FieldDocCommandsAreRecordedAsFormerNamesAndMetadata()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        var dial = (StructDescription)types.Single(type => type.Name == "Fixture::Dial");
        var fill = dial.Fields.Single(field => field.Name == "fill");
        var ticks = dial.Fields.Single(field => field.Name == "ticks");

        Assert.Equal(["level", "amount"], fill.Formerly);
        Assert.Equal(new FieldMetadata((0, 100), 0.5, "Fill level", "Readout", true, true, true, "percent"), fill.Metadata);
        Assert.Equal("Every metadata command on one field, and two former names.", fill.Doc);
        Assert.Equal(FieldMetadata.None with { Range = (-10, 10.5) }, ticks.Metadata);
        Assert.Empty(ticks.Formerly);
    }

    [Fact]
    public void NoRecordedDocTextHoldsADocCommand()
    {
        var described = DescriptionWriter.Write(Describer.Describe(Selection.Read(FixtureSelection), []));

        var command = new System.Text.RegularExpressions.Regex(
            "@(version|formerly|transient|editor-only|range|step|display|group|readonly|hidden|multiline|units)\\b");
        foreach (var line in described.Split('\n').Where(line => line.TrimStart().StartsWith("\"doc\":", StringComparison.Ordinal)))
        {
            Assert.DoesNotMatch(command, line);
        }
    }

    [Fact]
    public void AScalarDefaultMayBeAnyConstantExpression()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        var dial = (StructDescription)types.Single(type => type.Name == "Fixture::Dial");
        DefaultValue Of(string name) => dial.Fields.Single(field => field.Name == name).Default;

        Assert.Equal(new DefaultValue.Number("12"), Of("ticks"));
        Assert.Equal(new DefaultValue.Number("8"), Of("span"));
        Assert.Equal(new DefaultValue.Bool(true), Of("enabled"));
        Assert.Equal(new DefaultValue.Number("6"), Of("scaled"));
    }

    [Fact]
    public void ABoundTypeRecordsItsScalarComponentsWithOffsets()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        IReadOnlyList<BoundComponent>? Of(string name) => ((BoundTypeDescription)types.Single(type => type.Name == name)).Components;

        Assert.Equal([new("x", 0, "f32"), new("y", 4, "f32"), new("z", 8, "f32")], Of("Fixture::Vec3"));
        Assert.Equal([new("x", 0, "f32"), new("y", 4, "f32"), new("z", 8, "f32"), new("w", 12, "f32")], Of("Fixture::Quat"));
        Assert.Equal([new("first", 0, "f64"), new("second", 8, "f64")], Of("Fixture::Pair<double>"));
        Assert.Equal([new("m_number", 0, "u32")], Of("Fixture::Ticket"));
        Assert.Equal([new("m_value[0].x", 0, "f32"), new("m_value[0].y", 4, "f32"), new("m_value[0].z", 8, "f32"),
                      new("m_value[1].x", 12, "f32"), new("m_value[1].y", 16, "f32"), new("m_value[1].z", 20, "f32")],
                     Of("Fixture::Columns"));
        // A pointer is not a value a visitor presents, so a bound type holding one records no components.
        Assert.Null(Of("Fixture::Handle"));
    }

    [Fact]
    public void AnEntityAFixedStringAndAnAssetHandleAreRecordedAsTheirOwnKinds()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        var socket = (StructDescription)types.Single(type => type.Name == "Fixture::Socket");
        FieldDescription Of(string name) => socket.Fields.Single(field => field.Name == name);

        Assert.Equal(new FieldType("entity"), Of("owner").Type);
        Assert.Equal(new DefaultValue.Null(), Of("owner").Default);
        Assert.Equal(new DefaultValue.Null(), Of("target").Default);
        Assert.Equal(16, Of("target").Offset - Of("owner").Offset);
        Assert.Equal(new FieldType("fixed-string", Length: 32), Of("label").Type);
        Assert.Equal(new DefaultValue.Text("Socket"), Of("label").Default);
        Assert.Equal(new FieldType("fixed-string", Length: 8), Of("title").Type);
        Assert.Equal(new DefaultValue.Text(""), Of("title").Default);
        Assert.Equal(new FieldType("reference", Target: "Fenrir::Mesh"), Of("mesh").Type);
        Assert.Equal(new DefaultValue.Null(), Of("mesh").Default);
        Assert.Equal(new DefaultValue.Null(), Of("lod").Default);
        Assert.Equal(8, Of("lod").Offset - Of("mesh").Offset);
        // The kinds name no described type: the engine's entity and handle are never described for them.
        Assert.DoesNotContain(types, type => type.Name.StartsWith("Fenrir::", StringComparison.Ordinal));
    }

    [Fact]
    public void BytesStayAnArrayOfU8AndACharArrayIsAFixedStringItsCharactersSpell()
    {
        var types = Describer.Describe(Selection.Read(FixtureSelection), []).Types;
        var socket = (StructDescription)types.Single(type => type.Name == "Fixture::Socket");
        var body = (StructDescription)types.Single(type => type.Name == "Fixture::Body");
        var tag = (StructDescription)types.Single(type => type.Name == "Fixture::Tag");

        Assert.Equal(new FieldType("array", Element: new FieldType("u8"), Length: 4),
                     socket.Fields.Single(field => field.Name == "bytes").Type);
        Assert.Equal(new FieldType("array", Element: new FieldType("u8"), Length: 3),
                     body.Fields.Single(field => field.Name == "zeros").Type);
        var label = tag.Fields.Single(field => field.Name == "m_label");
        Assert.Equal((new FieldType("fixed-string", Length: 4), (DefaultValue)new DefaultValue.Text("a")),
                     (label.Type, label.Default));
    }

    [Fact]
    public void ARegistrationTableIsAcceptedAndAnUnknownTableStillFails()
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "Registered.api.toml");
        const string types = "[types]\n\"Failing::NoInitialiser\" = \"component\"\n";
        File.WriteAllText(selection, $"headers = [\"{header}\"]\n\n{types}\n[registration]\nheader = \"x.hpp\"\n" +
                                     "source = \"x.cpp\"\nfunction = \"Register\"\nincludes = [\"a.hpp\"]\n");

        Assert.Equal("Registered", Selection.Read(selection).Package);

        File.WriteAllText(selection, $"headers = [\"{header}\"]\n\n{types}\n[other]\nkey = 1\n");
        var exception = Assert.Throws<DescribeException>(() => Selection.Read(selection));
        Assert.Contains("unknown key 'other'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ARegistrationThatIsNotATableFails()
    {
        using var directory = new TemporaryDirectory();
        var header = Path.Combine(FixtureDirectory(), "Failing.hpp").Replace('\\', '/');
        var selection = Path.Combine(directory.Path, "Registered.api.toml");
        File.WriteAllText(selection, $"registration = 1\nheaders = [\"{header}\"]\n\n[types]\n\"Failing::NoInitialiser\" = \"component\"\n");

        var exception = Assert.Throws<DescribeException>(() => Selection.Read(selection));

        Assert.Contains("'registration' is a table", exception.Message, StringComparison.Ordinal);
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

    [Fact]
    public void ACharIsU8ByPointerAndByValueWhateverTheHostsSignedness()
    {
        var selection = NativesSelection.Read(Path.Combine(FixtureDirectory(), "Fixture.binding.toml"));
        var say = NativesDescriber.Describe(selection, []).Functions.Single(function => function.Name == "Say");
        var text = say.Parameters.Single(parameter => parameter.Name == "text").Type;
        var terminator = say.Parameters.Single(parameter => parameter.Name == "terminator").Type;

        Assert.Equal(("const char*", "pointer", "char", "u8"), (text.Spelling, text.Kind, text.Pointee, text.PointeeKind));
        Assert.Equal(("char", "u8"), (terminator.Spelling, terminator.Kind));
    }

    [Fact]
    public void AFacadeMarksReturnsNamesTheSingleOutputTheMethodGivesBack()
    {
        var selection = NativesSelection.Read(Path.Combine(FixtureDirectory(), "Fixture.binding.toml"));
        var functions = NativesDescriber.Describe(selection, []).Functions;
        var nearest = functions.Single(function => function.Name == "Nearest");
        var find = functions.Single(function => function.Name == "Find");

        Assert.Equal(("Hits.Nearest", "hit"), (nearest.Facade, nearest.FacadeReturns));
        Assert.Equal(("Hits.Find", null), (find.Facade, find.FacadeReturns));
    }

    [Fact]
    public void TheCallContextComesFromTheDefaultsFileWhenTheBindingStatesNone()
    {
        using var directory = new TemporaryDirectory();
        var source = Path.Combine(FixtureDirectory(), "Natives.cpp").Replace('\\', '/');
        File.WriteAllText(Path.Combine(directory.Path, "Host.binding-defaults.toml"), "[natives]\ncontext = \"Host::CallContext\"\n");
        var config = Path.Combine(directory.Path, "Fixture.binding.toml");
        File.WriteAllText(config, $"defaults = \"Host.binding-defaults.toml\"\n[natives]\nsources = [\"{source}\"]\n");

        Assert.Equal("Host::CallContext", NativesSelection.Read(config).Context);

        File.WriteAllText(config, $"[natives]\nsources = [\"{source}\"]\n");
        var exception = Assert.Throws<DescribeException>(() => NativesSelection.Read(config));
        Assert.Contains("stated here or in the file 'defaults' names", exception.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("Hidden: has internal linkage (static, or in an anonymous namespace): the generated registration " +
                "declares and names it from another source file")]
    [InlineData("ByReference::value: is a reference: after the call context and the extension state, a native takes " +
                "values and pointers")]
    [InlineData("Unnamed (parameter 2): is unnamed: a native's parameters are named in C#")]
    [InlineData("OutValue::value: is documented @param[out] but is not a pointer to non-const data")]
    [InlineData("InOutConst::values: is documented @param[in,out] but is not a pointer to non-const data")]
    [InlineData("InValue::value: is documented @param[in] but is not a pointer to const data: [in] marks one value a " +
                "native reads through a pointer, which a count after it does not make a span")]
    [InlineData("NotRunningVoid: documents @notrunning but returns nothing")]
    [InlineData("DocumentsMissing: documents a parameter 'missing' it does not have")]
    [InlineData("PointerToPointer::values: is a pointer to a pointer, which the description cannot record")]
    [InlineData("Wide::wide: its type 'long double' is not a kind the description records")]
    [InlineData("TextByPointer::text: is a pointer to a std::string_view: a native takes text as a std::string_view by value")]
    [InlineData("MalformedFacade: @facade names a public method as Class.Method, followed at most by 'returns <parameter>'")]
    [InlineData("DoubledFacade: is marked @facade twice; a native names one public method")]
    [InlineData("ReturnsMissing: @facade returns 'missing', which names no parameter of the native")]
    [InlineData("ReturnsInput: @facade returns 'origin', which is not a single output (a pointer documented @param[out])")]
    [InlineData("ReturnsOneOfTwo: @facade returns one output, but the native has more than one: the method could return only one")]
    [InlineData("ReturnsBesideCount: @facade returns an output, but the native's result is std::int32_t, which the method " +
                "would lose: only a bool result, or none, is dropped")]
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
