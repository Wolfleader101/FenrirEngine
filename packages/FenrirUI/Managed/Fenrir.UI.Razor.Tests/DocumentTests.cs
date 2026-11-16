using Fenrir.Scripting;
using Xunit;

namespace Fenrir.UI.Razor.Tests;

// <Document Src> through the recording fake of the element seam (spec: "The builder" unowned children; ADR-0067;
// ticket 08): a host panel the document loads into, whose children the builder never visits, updates or removes, except
// when Src changes (the old content removed, the new loaded) and with the host.
public sealed class DocumentTests : IDisposable
{
    private static readonly ElementId Parent = RecordingSeam.Id(100);

    private readonly RecordingSeam seam = new();
    private readonly List<string> messages = [];

    public DocumentTests()
    {
        RazorRuntime.Release();
        RazorDiagnostics.Sink = (_, message) => messages.Add(message);
        seam.Documents["credits.ui"] = ["credit"];
        seam.Documents["other.ui"] = ["other"];
    }

    public void Dispose()
    {
        RazorRuntime.Release();
        RazorDiagnostics.Sink = null;
    }

    private ComponentRef<T> Mount<T>(Action<T>? init = null) where T : UIComponent, new() =>
        new(RazorRuntime.Mount(default, seam, Parent, -1, init));

    private List<string> RunFrame()
    {
        seam.Calls.Clear();
        RazorRuntime.Build(default(SystemContext));
        List<string> calls = [.. seam.Calls];
        seam.Calls.Clear();
        Assert.Empty(messages);
        return calls;
    }

    private List<string> Rebuild(UIComponent component)
    {
        component.StateHasChanged();
        return RunFrame();
    }

    [Fact]
    public void ADocumentLoadsIntoAHostPanelInItsPlace()
    {
        Mount<Placed>();

        Assert.Equal(
            [
                "CreateElement(#1, label, -1) = #2",
                "AddClass(#2, title)",
                "SetText(#2, Credits)",
                "CreateElement(#1, panel, -1) = #3",
                "AddClass(#3, document)",
                "LoadDocument(#3, credits.ui)",
                "CreateElement(#1, label, -1) = #5",
                "AddClass(#5, after)",
                "SetText(#5, after)",
            ],
            RunFrame());
        Assert.Equal(["#4"], seam.ChildrenOf(RecordingSeam.Id(3)));
        Assert.Equal(RecordingSeam.Id(4), seam.ElementWithId("credit"));
    }

    [Fact]
    public void ARebuildLeavesTheDocumentsElementsAlone()
    {
        Placed placed = Mount<Placed>().Instance!;
        RunFrame();

        placed.Title = "Thanks";
        Assert.Equal(["SetText(#2, Thanks)"], Rebuild(placed));
        Assert.Equal(["#4"], seam.ChildrenOf(RecordingSeam.Id(3)));
    }

    [Fact]
    public void ChangingSrcRemovesTheOldContentAndLoadsTheNew()
    {
        Placed placed = Mount<Placed>().Instance!;
        RunFrame();

        placed.Src = "other.ui";
        Assert.Equal(["GetChildren(#3)", "Remove(#4, Immediate)", "LoadDocument(#3, other.ui)"], Rebuild(placed));
        Assert.Equal(["#6"], seam.ChildrenOf(RecordingSeam.Id(3)));
        Assert.False(seam.Exists(RecordingSeam.Id(4)));
    }

    [Fact]
    public void NoSrcRemovesTheContentAndLoadsNothing()
    {
        Placed placed = Mount<Placed>().Instance!;
        RunFrame();

        placed.Src = null;
        Assert.Equal(["GetChildren(#3)", "Remove(#4, Immediate)"], Rebuild(placed));
        Assert.Empty(seam.ChildrenOf(RecordingSeam.Id(3)));

        placed.Src = "";
        Assert.Empty(Rebuild(placed));
    }

    [Fact]
    public void ADocumentThatFailsToLoadIsNotLoggedAgainNorRetried()
    {
        Placed placed = Mount<Placed>(p => p.Src = "missing.ui").Instance!;
        Assert.Contains("LoadDocument(#3, missing.ui)", RunFrame());

        placed.Title = "Again";
        Assert.Equal(["SetText(#2, Again)"], Rebuild(placed));
    }

    [Fact]
    public void ADocumentNoLongerPlacedGoesWithItsHost()
    {
        Placed placed = Mount<Placed>().Instance!;
        RunFrame();

        placed.Shown = false;
        Assert.Equal(["Remove(#3, Outro)"], Rebuild(placed));
        seam.FinishOutros();
        Assert.False(seam.Exists(RecordingSeam.Id(4)));
    }

    [Fact]
    public void UnmountingRemovesTheDocumentWithTheMountsHost()
    {
        ComponentRef<Placed> mounted = Mount<Placed>();
        RunFrame();

        mounted.Unmount();
        Assert.Equal(["Remove(#1, Outro)"], seam.Calls);
        seam.FinishOutros();
        Assert.False(seam.Exists(RecordingSeam.Id(4)));
    }

    [Fact]
    public void ADocumentPlacedAgainLoadsAgainInItsPlace()
    {
        Placed placed = Mount<Placed>().Instance!;
        RunFrame();

        placed.Shown = false;
        Rebuild(placed);
        seam.FinishOutros();
        placed.Shown = true;
        Assert.Equal(
            [
                "GetChildren(#1)",
                "CreateElement(#1, panel, 1) = #6",
                "AddClass(#6, document)",
                "LoadDocument(#6, credits.ui)",
            ],
            Rebuild(placed));
        Assert.Equal(["#2", "#6", "#5"], seam.ChildrenOf(RecordingSeam.Id(1)));
        Assert.Equal(["#7"], seam.ChildrenOf(RecordingSeam.Id(6)));
    }

    [Fact]
    public void ADocumentMountedOnItsOwnLoadsUnderItsMountsHost()
    {
        Mount<Document>(document => document.Src = "credits.ui");

        Assert.Equal(
            ["CreateElement(#1, panel, -1) = #2", "AddClass(#2, document)", "LoadDocument(#2, credits.ui)"],
            RunFrame());
    }
}
