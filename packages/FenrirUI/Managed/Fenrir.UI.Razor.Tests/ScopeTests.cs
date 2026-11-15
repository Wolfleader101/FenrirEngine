using System.Reflection;
using Fenrir.Scripting;
using Fenrir.UI.Razor.Tests.Sheets;
using Microsoft.AspNetCore.Components.Rendering;
using Xunit;

namespace Fenrir.UI.Razor.Tests;

// Scoped component stylesheets through the recording fake of the element seam (spec: "Scoped component stylesheets";
// ADR-0070): the scope class on every element a Razor component's markup makes, its sheet loaded once per root and type
// on its first mount there (by path when the file is there, from its text otherwise), and <style> blocks the same way.
public sealed class ScopeTests : IDisposable
{
    private static readonly ElementId Parent = RecordingSeam.Id(100);

    private readonly RecordingSeam seam = new() { RecordScopes = true };
    private readonly List<string> messages = [];

    public ScopeTests()
    {
        RazorRuntime.Release();
        RazorDiagnostics.Sink = (_, message) => messages.Add(message);
        seam.Documents["credits.ui"] = ["credit"];
    }

    public void Dispose()
    {
        RazorRuntime.Release();
        RazorDiagnostics.Sink = null;
    }

    private ComponentRef<T> Mount<T>(Action<T>? init = null) where T : UIComponent, new() =>
        new(RazorRuntime.Mount(default, seam, Parent, -1, init));

    private List<string> Calls()
    {
        List<string> calls = [.. seam.Calls];
        seam.Calls.Clear();
        return calls;
    }

    private List<string> RunFrame()
    {
        seam.Calls.Clear();
        RazorRuntime.Build(default(SystemContext));
        Assert.Empty(messages);
        return Calls();
    }

    private static string Scope<T>() => ComponentScope.ClassOf(typeof(T));

    private static string SheetPath<T>() => typeof(T).GetCustomAttribute<ComponentStylesheetAttribute>()!.Path;

    [Theory]
    [InlineData("", "s-811c9dc5")]
    [InlineData("a", "s-e40c292c")]
    [InlineData("Fenrir.UI.Razor.Tests.Sheets.Titled", "s-49e56a86")]
    public void TheScopeClassIsFnv1aOfTheFullNameInEightHexDigits(string fullName, string scope)
    {
        Assert.Equal(scope, ComponentScope.ClassOf(fullName));
    }

    [Fact]
    public void AComponentTypesScopeClassIsItsFullNames()
    {
        Assert.Equal(ComponentScope.ClassOf("Fenrir.UI.Razor.Tests.Sheets.Titled"), Scope<Titled>());
    }

    [Fact]
    public void TheHostAndEveryElementTheMarkupMakesCarryTheScopeClass()
    {
        Mount<Titled>();
        Assert.Equal(
            [
                "CreateElement(#100, panel, -1) = #1",
                "AddClass(#1, titled)",
                $"AddScope(#1, {Scope<Titled>()})",
                $"LoadStylesheet({SheetPath<Titled>()}, {Scope<Titled>()})",
            ],
            Calls());

        RunFrame();

        // The label, the folded panel and its label, and the loose text's generated label.
        for (uint id = 1; id <= 5; ++id)
            Assert.Equal(Scope<Titled>(), seam.ScopeOf(RecordingSeam.Id(id)));
    }

    [Fact]
    public void AChildsElementsCarryItsScopeAndAFragmentsElementsTheWritersScope()
    {
        Mount<Holder>();
        Calls();

        Assert.Equal(
            [
                "CreateElement(#1, panel, -1) = #2",
                $"AddScope(#2, {Scope<Holder>()})",
                "AddClass(#2, list)",
                $"LoadStylesheet({SheetPath<Wrapper>()}, {Scope<Wrapper>()})",
                "CreateElement(#2, panel, -1) = #3",
                $"AddScope(#3, {Scope<Wrapper>()})",
                "AddClass(#3, frame)",
                "CreateElement(#3, label, -1) = #4",
                $"AddScope(#4, {Scope<Holder>()})",
                "AddClass(#4, mine)",
                "SetText(#4, from holder)",
            ],
            RunFrame());
    }

    [Fact]
    public void APlacedDocumentsElementsCarryNoScope()
    {
        Mount<Placed>();
        RunFrame();

        Assert.Null(seam.ScopeOf(seam.ElementWithId("credit")));
    }

    [Fact]
    public void ASecondMountOfTheTypeIntoTheRootLoadsNoSecondSheet()
    {
        Mount<Titled>();
        RunFrame();

        Mount<Titled>();
        Assert.DoesNotContain(Calls(), call => call.StartsWith("LoadStylesheet") || call.StartsWith("AddStylesheet"));
    }

    [Fact]
    public void AMountIntoAnotherRootLoadsTheSheetThereToo()
    {
        Mount<Titled>();
        var other = new RecordingSeam();

        RazorRuntime.Mount<Titled>(new UIRoot { Value = 7 }, other, Parent, -1, null);

        Assert.Contains($"LoadStylesheet({SheetPath<Titled>()}, {Scope<Titled>()})", other.Calls);
    }

    [Fact]
    public void ASheetWhoseFileIsGoneLoadsFromItsText()
    {
        Mount<Moved>();

        Assert.Equal(
            $"AddStylesheet(C:/nowhere/Moved.razor.css, {Scope<Moved>()}): .moved {{ color: red; }}", Calls()[^1]);
    }

    [Fact]
    public void ASheetTheRootRefusesByPathLoadsFromItsText()
    {
        seam.LoadsStylesheets = false;

        Mount<Titled>();

        List<string> calls = Calls();
        Assert.Equal($"LoadStylesheet({SheetPath<Titled>()}, {Scope<Titled>()})", calls[^2]);
        Assert.Equal($"AddStylesheet({SheetPath<Titled>()}, {Scope<Titled>()}): .title {{ color: red; }}\n", calls[^1]);
    }

    [Fact]
    public void AStyleBlockIsAddedScopedOncePerRootAndType()
    {
        Styled styled = Mount<Styled>().Instance!;
        Calls();

        List<string> first = RunFrame();
        Assert.Contains(
            $"AddStylesheet({typeof(Styled).FullName}.razor <style> at sequence 0, {Scope<Styled>()}): .a {{ color: blue; }}",
            first);

        styled.StateHasChanged();
        Assert.Empty(RunFrame());

        Mount<Styled>();
        Assert.DoesNotContain(RunFrame(), call => call.StartsWith("AddStylesheet"));
    }

    [Fact]
    public void TheSheetsLoadedAreForgottenOnRelease()
    {
        Mount<Titled>();
        RazorRuntime.Release();
        Calls();

        Mount<Titled>();

        Assert.Contains($"LoadStylesheet({SheetPath<Titled>()}, {Scope<Titled>()})", Calls());
    }

    [Fact]
    public void TheGeneratorRecordsEachSheetWithItsAbsolutePathAndText()
    {
        ComponentStylesheetAttribute sheet = typeof(Titled).GetCustomAttribute<ComponentStylesheetAttribute>()!;

        Assert.True(Path.IsPathRooted(sheet.Path));
        Assert.EndsWith("Sheets/Titled.razor.css", sheet.Path.Replace('\\', '/'));
        Assert.Equal(File.ReadAllText(sheet.Path), sheet.Text);
        Assert.Null(typeof(Styled).GetCustomAttribute<ComponentStylesheetAttribute>());
    }

    [Theory]
    [InlineData(typeof(Some.Where.Spaced))]
    [InlineData(typeof(Imported.Space.Deeper.Inner))]
    public void TheGeneratorNamesTheComponentAsRazorDoes(Type component)
    {
        // A name Razor did not give would make a class of its own, with no markup and no base.
        Assert.True(component.IsSubclassOf(typeof(UIComponent)));
        Assert.NotNull(component.GetCustomAttribute<ComponentStylesheetAttribute>());
    }

    /// <summary>A Razor component whose sheet's file is not where it was built, as in a shipped game.</summary>
    [ComponentStylesheet("C:/nowhere/Moved.razor.css", ".moved { color: red; }")]
    private sealed class Moved : UIComponent
    {
        protected override void BuildRenderTree(RenderTreeBuilder __builder)
        {
        }
    }
}
