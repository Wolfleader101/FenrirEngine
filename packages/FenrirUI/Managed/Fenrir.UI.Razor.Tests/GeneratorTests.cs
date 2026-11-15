using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using Fenrir.UI.Razor.Generators;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Diagnostics;
using Microsoft.CodeAnalysis.Text;
using Xunit;

namespace Fenrir.UI.Razor.Tests;

// FenrirUI's Razor generator driven over files in memory (spec: "Fenrir.UI.Razor.props and the generators"; ADR-0070):
// each Foo.razor.css beside a Foo.razor is a partial Foo with [ComponentStylesheet(path, text)], named as Razor names Foo.
// The components compiled here (Sheets/, Spaced/, Imported/) check the same against the SDK's own Razor generator.
public sealed class GeneratorTests
{
    private const string Project = "C:/game/Scripts";

    private sealed class File(string path, string text) : AdditionalText
    {
        public override string Path { get; } = path;

        public override SourceText GetText(CancellationToken cancellationToken = default) => SourceText.From(text);
    }

    private sealed class Options(Dictionary<string, string> values) : AnalyzerConfigOptions
    {
        public override bool TryGetValue(string key, [NotNullWhen(true)] out string? value) => values.TryGetValue(key, out value);
    }

    private sealed class Provider(AnalyzerConfigOptions global) : AnalyzerConfigOptionsProvider
    {
        private static readonly Options None = new([]);

        public override AnalyzerConfigOptions GlobalOptions { get; } = global;

        public override AnalyzerConfigOptions GetOptions(SyntaxTree tree) => None;

        public override AnalyzerConfigOptions GetOptions(AdditionalText textFile) => None;
    }

    private static GeneratorDriverRunResult Run(params (string Path, string Text)[] files)
    {
        var global = new Options(new Dictionary<string, string>
        {
            ["build_property.RootNamespace"] = "Game",
            ["build_property.MSBuildProjectDirectory"] = Project.Replace('/', '\\'),
        });
        GeneratorDriver driver = CSharpGeneratorDriver.Create(
            [new ComponentStylesheetGenerator().AsSourceGenerator()],
            files.Select(file => (AdditionalText)new File($"{Project}/{file.Path}", file.Text)).ToImmutableArray(),
            optionsProvider: new Provider(global));
        return driver.RunGenerators(CSharpCompilation.Create("Scripts")).GetRunResult();
    }

    private static string Only(GeneratorDriverRunResult result)
    {
        Assert.Empty(result.Diagnostics);
        return Assert.Single(Assert.Single(result.Results).GeneratedSources).SourceText.ToString();
    }

    [Fact]
    public void ASheetBesideAComponentIsRecordedOnItWithItsPathAndText()
    {
        string source = Only(Run(("Hud/Health.razor", "<label>hp</label>"), ("Hud/Health.razor.css", ".bar { color: \"red\"; }")));

        Assert.Contains("namespace Game.Hud;", source);
        Assert.Contains("partial class Health", source);
        Assert.Contains($"[global::Fenrir.UI.Razor.ComponentStylesheet(@\"{Project}/Hud/Health.razor.css\", " +
                        "@\".bar { color: \"\"red\"\"; }\")]", source);
    }

    [Fact]
    public void TheNamespaceInTheFileNamesTheComponent()
    {
        string source = Only(Run(("Hud/Health.razor", "@* @namespace Not.This *@\n@namespace Ui.Bars\n<label/>"),
                                 ("Hud/Health.razor.css", "")));

        Assert.Contains("namespace Ui.Bars;", source);
    }

    [Fact]
    public void TheNearestImportsNamespaceNamesTheComponentWithTheFoldersBelowIt()
    {
        string source = Only(Run(("_Imports.razor", "@namespace Top"), ("Hud/_Imports.razor", "@using X\n@namespace Ui"),
                                 ("Hud/Bars/Health.razor", "<label/>"), ("Hud/Bars/Health.razor.css", "")));

        Assert.Contains("namespace Ui.Bars;", source);
    }

    [Fact]
    public void FolderNamesBecomeIdentifiersAsRazorMakesThem()
    {
        string source = Only(Run(("2d/my-hud/Health.razor", "<label/>"), ("2d/my-hud/Health.razor.css", "")));

        Assert.Contains("namespace Game._2d.my_hud;", source);
    }

    [Fact]
    public void AGenericComponentsPartialHasItsTypeParameters()
    {
        string source = Only(Run(("List.razor", "@typeparam TItem\n@typeparam TKey\n<label/>"), ("List.razor.css", "")));

        Assert.Contains("partial class List<TItem, TKey>", source);
    }

    [Fact]
    public void ASheetWithNoComponentBesideItIsWarnedAbout()
    {
        GeneratorDriverRunResult result = Run(("Hud/Health.razor", "<label/>"), ("Hud/Mana.razor.css", ".a {}"));

        Diagnostic warning = Assert.Single(result.Diagnostics);
        Assert.Equal("FUIR001", warning.Id);
        Assert.Equal(DiagnosticSeverity.Warning, warning.Severity);
        Assert.Contains("Mana.razor", warning.GetMessage());
        Assert.Empty(Assert.Single(result.Results).GeneratedSources);
    }

    [Fact]
    public void AComponentWithNoSheetGetsNothing()
    {
        GeneratorDriverRunResult result = Run(("Hud/Health.razor", "<label/>"));

        Assert.Empty(result.Diagnostics);
        Assert.Empty(Assert.Single(result.Results).GeneratedSources);
    }
}
