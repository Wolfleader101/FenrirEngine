using Fenrir.Scripting;
using Microsoft.AspNetCore.Components.Rendering;
using Xunit;

namespace Fenrir.UI.Razor.Tests;

// Rebuilds in place through the recording fake of the element seam (spec: "The builder", "Components, mounting and the
// dispatcher"; ticket 04): each test pins the native calls one change makes, and a change of nothing makes none.
public sealed class RebuildTests : IDisposable
{
    private static readonly ElementId Parent = RecordingSeam.Id(100);

    private readonly RecordingSeam seam = new();
    private readonly List<string> warnings = [];
    private readonly List<string> errors = [];

    public RebuildTests()
    {
        RazorRuntime.Release();
        RazorDiagnostics.Sink = (error, message) => (error ? errors : warnings).Add(message);
    }

    public void Dispose()
    {
        RazorRuntime.Release();
        RazorDiagnostics.Sink = null;
    }

    private T Mount<T>(Action<T>? init = null) where T : UIComponent, new() =>
        (T)RazorRuntime.Mount(default, seam, Parent, -1, init).Component;

    private List<string> Frame()
    {
        seam.Calls.Clear();
        RazorRuntime.Build(default(SystemContext));
        List<string> calls = [.. seam.Calls];
        seam.Calls.Clear();
        return calls;
    }

    private List<string> Rebuild(UIComponent component)
    {
        component.StateHasChanged();
        return Frame();
    }

    // ---- Values ----

    [Fact]
    public void ARebuildThatChangesNothingMakesNoNativeCall()
    {
        HudPanel hud = Mount<HudPanel>();
        Frame();

        Assert.Empty(Rebuild(hud));
        Assert.Empty(Rebuild(hud));
    }

    [Fact]
    public void ATextChangeIsOneSetText()
    {
        Readout readout = Mount<Readout>();
        Assert.Equal(
            [
                "CreateElement(#1, panel, -1) = #2",
                "AddClass(#2, row)",
                "SetStyle(#2, width, 100px)",
                "SetStyle(#2, height, 40px)",
                "SetIdString(#2, row)",
                "CreateElement(#2, label, -1) = #3",
                "SetText(#3, Count: 0)",
            ],
            Frame());

        readout.Count = 1;

        Assert.Equal(["SetText(#3, Count: 1)"], Rebuild(readout));
    }

    [Fact]
    public void AClassTokenIsAddedAndRemovedAlone()
    {
        Readout readout = Mount<Readout>();
        Frame();

        readout.Active = true;
        Assert.Equal(["AddClass(#2, active)"], Rebuild(readout));

        readout.Active = false;
        Assert.Equal(["RemoveClass(#2, active)"], Rebuild(readout));
    }

    [Fact]
    public void AChangedDeclarationIsSetAndADroppedOneReset()
    {
        Readout readout = Mount<Readout>();
        Frame();

        readout.Width = 120;
        readout.Tall = false;

        Assert.Equal(["SetStyle(#2, height, )", "SetStyle(#2, width, 120px)"], Rebuild(readout));
        Assert.Empty(Rebuild(readout));
    }

    [Fact]
    public void AValueIsSetByTagAndOnlyOnChange()
    {
        Conditional conditional = Mount<Conditional>();
        Assert.Equal(
            [
                "CreateElement(#1, panel, -1) = #2",
                "SetIdString(#2, box)",
                "CreateElement(#2, label, -1) = #3",
                "SetIdString(#3, first)",
                "SetText(#3, First)",
                "CreateElement(#2, slider, -1) = #4",
                "SetValue(#4, 0.5)",
                "CreateElement(#2, textentry, -1) = #5",
                "SetEntryText(#5, Ada)",
            ],
            Frame());

        conditional.Volume = 0.25f;
        conditional.Name = "Grace";

        Assert.Equal(["SetValue(#4, 0.25)", "SetEntryText(#5, Grace)"], Rebuild(conditional));
    }

    [Fact]
    public void DroppedAttributesAndIdAreUndone()
    {
        Handwritten written = Mount<Handwritten>(w =>
        {
            w.Disabled = true;
            w.Tooltip = "Close";
            w.Id = "x";
        });
        Assert.Equal(
            [
                "CreateElement(#1, button, -1) = #2",
                "SetAttribute(#2, disabled, true)",
                "SetAttribute(#2, tooltip, Close)",
                "SetIdString(#2, x)",
            ],
            Frame());

        written.Disabled = false;
        written.Tooltip = null;
        written.Id = null;

        Assert.Equal(["SetIdString(#2, )", "SetAttribute(#2, tooltip, )", "SetAttribute(#2, disabled, false)"],
                     Rebuild(written));
        Assert.Empty(Rebuild(written));
    }

    // ---- Text ----

    [Fact]
    public void LooseTextInAPanelIsAGeneratedLabelPerRun()
    {
        LooseText loose = Mount<LooseText>();
        Assert.Equal(
            [
                "CreateElement(#1, panel, -1) = #2",
                "SetIdString(#2, loose)",
                "CreateElement(#2, label, -1) = #3",
                "SetText(#3, Hello Ada)",
                "CreateElement(#2, label, -1) = #4",
                "SetIdString(#4, mid)",
                "SetText(#4, -)",
                "CreateElement(#2, label, -1) = #5",
                "SetText(#5, bye)",
            ],
            Frame());

        loose.Name = "Bob";

        Assert.Equal(["SetText(#3, Hello Bob)"], Rebuild(loose));
    }

    [Fact]
    public void AGeneratedLabelKeepsItsKeyWhenItsFirstPartIsEmpty()
    {
        TwoParts parts = Mount<TwoParts>(p => p.First = "");
        Assert.Equal(["CreateElement(#1, label, -1) = #2", "SetText(#2, b)"], Frame());

        parts.First = "a";

        Assert.Equal(["SetText(#2, ab)"], Rebuild(parts));
    }

    // ---- Keys, removal and order ----

    [Fact]
    public void AKeyedRowIsAppendedWithItsFoldedLabelAndRemovedWithOneRemove()
    {
        KeyedRows rows = Mount<KeyedRows>(r => r.Rows = ["a", "b"]);
        Assert.Equal(
            [
                "CreateElement(#1, panel, -1) = #2",
                "SetIdString(#2, rows)",
                "CreateElement(#2, panel, -1) = #3",
                "AddClass(#3, row)",
                "CreateElement(#3, label, -1) = #4",
                "AddClass(#4, static)",
                "SetText(#4, Row & more)",
                "CreateElement(#3, label, -1) = #5",
                "SetText(#5, a)",
                "CreateElement(#2, panel, -1) = #6",
                "AddClass(#6, row)",
                "CreateElement(#6, label, -1) = #7",
                "AddClass(#7, static)",
                "SetText(#7, Row & more)",
                "CreateElement(#6, label, -1) = #8",
                "SetText(#8, b)",
            ],
            Frame());
        Assert.Empty(Rebuild(rows));

        rows.Rows = ["a", "b", "c"];
        Assert.Equal(
            [
                "CreateElement(#2, panel, -1) = #9",
                "AddClass(#9, row)",
                "CreateElement(#9, label, -1) = #10",
                "AddClass(#10, static)",
                "SetText(#10, Row & more)",
                "CreateElement(#9, label, -1) = #11",
                "SetText(#11, c)",
            ],
            Rebuild(rows));

        rows.Rows = ["b", "c"];
        Assert.Equal(["Remove(#3, Outro)"], Rebuild(rows));
    }

    [Fact]
    public void TwoReversedRowsAreOneReorderAndKeepTheirFoldedContent()
    {
        KeyedRows rows = Mount<KeyedRows>(r => r.Rows = ["a", "b"]);
        Frame();

        rows.Rows = ["b", "a"];

        Assert.Equal(["Reorder(#6, 0)"], Rebuild(rows));
        Assert.Equal(["#6", "#3"], seam.ChildrenOf(RecordingSeam.Id(2)));
        Assert.Empty(Rebuild(rows));
    }

    [Fact]
    public void IndicesCountARemovedRowWhileItIsOutgoing()
    {
        KeyedRows rows = Mount<KeyedRows>(r => r.Rows = ["a", "b", "c"]);
        Frame();
        rows.Rows = ["b", "c"];
        Assert.Equal(["Remove(#3, Outro)"], Rebuild(rows));

        rows.Rows = ["b", "x", "c"];
        List<string> calls = Rebuild(rows);

        Assert.Equal("GetChildren(#2)", calls[0]);
        Assert.Equal("CreateElement(#2, panel, 2) = #12", calls[1]);
        Assert.Equal(["#3", "#6", "#12", "#9"], seam.ChildrenOf(RecordingSeam.Id(2)));
        seam.FinishOutros();
        Assert.Equal(["#6", "#12", "#9"], seam.ChildrenOf(RecordingSeam.Id(2)));

        rows.Rows = ["c", "x", "b"];
        // b keeps its place, the longest run in order; x and then c move ahead of it.
        Assert.Equal(["GetChildren(#2)", "Reorder(#12, 0)", "Reorder(#9, 0)"], Rebuild(rows));
        Assert.Equal(["#9", "#12", "#6"], seam.ChildrenOf(RecordingSeam.Id(2)));
    }

    [Fact]
    public void AnIfElementIsMadeInPlaceAndRemovedWithItsOutro()
    {
        Conditional conditional = Mount<Conditional>();
        Frame();

        conditional.Show = true;
        Assert.Equal(
            ["CreateElement(#2, label, 1) = #6", "SetIdString(#6, middle)", "SetText(#6, Middle)"],
            Rebuild(conditional));
        Assert.Equal(["#3", "#6", "#4", "#5"], seam.ChildrenOf(RecordingSeam.Id(2)));

        conditional.Show = false;
        Assert.Equal(["Remove(#6, Outro)"], Rebuild(conditional));
        Assert.Empty(Rebuild(conditional));
    }

    [Fact]
    public void ATagChangeAtTheSameKeyReplacesTheElement()
    {
        Handwritten written = Mount<Handwritten>();
        Frame();

        written.Tag = "label";

        Assert.Equal(["Remove(#2, Outro)", "CreateElement(#1, label, -1) = #3"], Rebuild(written));
    }

    [Fact]
    public void TwoSiblingsWithOneKeyFailTheBuild()
    {
        Mount<KeyedRows>(r => r.Rows = ["a", "a"]);

        Frame();

        Assert.Contains("same @key", Assert.Single(errors));
    }

    // ---- Diagnostics ----

    [Fact]
    public void AnUnrecognisedDirectiveIsLeftOutAndWarnedAboutOncePerSequence()
    {
        MissingUsing first = Mount<MissingUsing>();
        Mount<MissingUsing>();

        List<string> calls = Frame();
        Rebuild(first);

        Assert.DoesNotContain(calls, call => call.Contains("onclick", StringComparison.Ordinal));
        string warning = Assert.Single(warnings);
        Assert.Contains(typeof(MissingUsing).FullName!, warning);
        Assert.Contains("\"@onclick\"", warning);
        Assert.Contains("@using Microsoft.AspNetCore.Components.Web", warning);
    }

    // ---- Triggers and hooks ----

    [Fact]
    public void TheHooksRunAroundTheBuilds()
    {
        Lifecycle life = Mount<Lifecycle>();
        Assert.Empty(life.Hooks);

        Frame();
        Frame();
        Rebuild(life);
        Assert.Equal(["mount", "built first", "built"], life.Hooks);

        Assert.True(RazorRuntime.Unmount(life.Mounted!, RemoveMode.Outro));
        Assert.Equal(["mount", "built first", "built", "unmount"], life.Hooks);
    }

    [Fact]
    public void AChangedBuildHashRebuildsAndAnUnchangedOneDoesNot()
    {
        Lifecycle life = Mount<Lifecycle>();
        Frame();

        life.Hash = 7;
        life.Value = 1;
        Assert.Equal(["SetText(#2, 1)"], Frame());
        Assert.Empty(Frame());
        Assert.Equal(["mount", "built first", "built"], life.Hooks);
    }

    [Fact]
    public void ARazorComponentBuildsAtMostOnceAFrame()
    {
        Lifecycle life = Mount<Lifecycle>();
        Frame();
        life.StateHasChanged();
        life.StateHasChanged();
        life.Hash = 3;
        Frame();
        Assert.Equal(["mount", "built first", "built"], life.Hooks);

        // Asked again from OnBuilt, it builds next frame, never twice in one.
        life.AskAgain = true;
        Rebuild(life);
        Frame();
        Assert.Equal(["mount", "built first", "built", "built", "built"], life.Hooks);
    }

    [Fact]
    public void AThrowingBuildTurnsOffOnlyItsRazorComponentUntilRelease()
    {
        Lifecycle failing = Mount<Lifecycle>();
        Lifecycle working = Mount<Lifecycle>();
        Frame();

        failing.Throw = true;
        failing.Value = 5;
        failing.StateHasChanged();
        working.Value = 6;
        Assert.Equal(["SetText(#4, 6)"], Rebuild(working));
        string error = Assert.Single(errors);
        Assert.Contains(typeof(Lifecycle).FullName!, error);
        Assert.Contains("The build threw", error);

        failing.Throw = false;
        Assert.Empty(Rebuild(failing));
        working.Value = 7;
        Assert.Equal(["SetText(#4, 7)"], Rebuild(working));
        Assert.Equal(["mount", "built first"], failing.Hooks);

        RazorRuntime.Release();
        Assert.Equal(["mount", "built first", "unmount"], failing.Hooks);
        Lifecycle again = Mount<Lifecycle>(l => l.Value = 8);
        Frame();
        Assert.Equal(["mount", "built first"], again.Hooks);
    }

    [Fact]
    public void ARootDestroyedDropsItsRazorComponents()
    {
        Lifecycle life = Mount<Lifecycle>();
        ComponentRef<Lifecycle> handle = new(life.Mounted!);
        Frame();

        seam.DestroyRoot();
        life.StateHasChanged();

        Assert.Empty(Frame());
        Assert.False(handle.IsMounted);
        Assert.Equal(["mount", "built first", "unmount"], life.Hooks);
        RazorRuntime.Release();
        Assert.Empty(seam.Calls);
        Assert.False(handle.Unmount());
    }

    [Fact]
    public void AHostRemovedBySomeoneElseDropsItsRazorComponentWhenItWouldBuild()
    {
        Lifecycle removed = Mount<Lifecycle>();
        Lifecycle kept = Mount<Lifecycle>();
        Frame();
        seam.Remove(removed.Element, RemoveMode.Immediate);

        removed.StateHasChanged();
        kept.Value = 2;
        kept.StateHasChanged();

        Assert.Equal(["SetText(#4, 2)"], Frame());
        Assert.Equal(["mount", "built first", "unmount"], removed.Hooks);
        Assert.Null(removed.Mounted);
        RazorRuntime.Release();
        Assert.Equal(["Remove(#2, Immediate)"], seam.Calls);
    }

    [Fact]
    public void TheRegistryChecksEachRootOnceAFrame()
    {
        Mount<Lifecycle>();
        Mount<Lifecycle>();
        Frame();
        int before = seam.AliveChecks;

        Frame();

        Assert.Equal(1, seam.AliveChecks - before);
    }

    [Fact]
    public void ReleaseRunsOnUnmountForMountedRazorComponents()
    {
        Lifecycle built = Mount<Lifecycle>();
        Lifecycle waiting;
        Frame();
        waiting = Mount<Lifecycle>();

        RazorRuntime.Release();

        Assert.Equal(["mount", "built first", "unmount"], built.Hooks);
        Assert.Empty(waiting.Hooks);
    }

    // ---- Markup ----

    [Fact]
    public void FoldedMarkupParsesElementsAttributesTextEntitiesAndVoidElements()
    {
        MarkupFragment fragment = MarkupFragment.Parse(
            "<panel class=\"a b\" data-x='1'>\n  <label>Tom &amp; Jerry &#39;s &#x41;</label><br><image src=\"i.png\" />" +
            "<!-- skipped --></panel>  <style>.a { color: red; }</style>");

        Assert.Equal(2, fragment.Nodes.Count);
        MarkupFragment.Node panel = fragment.Nodes[0];
        Assert.Equal("panel", panel.Text);
        Assert.Equal([KeyValuePair.Create("class", "a b"), KeyValuePair.Create("data-x", "1")], panel.Attributes);
        Assert.Equal(["label", "br", "image"], panel.Children.Select(child => child.Text));
        Assert.Equal("Tom & Jerry 's A", Assert.Single(panel.Children[0].Children).Text);
        Assert.Equal(MarkupFragment.NodeKind.Style, fragment.Nodes[1].Kind);
        Assert.Equal(".a { color: red; }", fragment.Nodes[1].Text);
    }

    [Theory]
    [InlineData("<panel>")]
    [InlineData("</panel>")]
    [InlineData("<panel class=\"a></panel>")]
    public void MalformedFoldedMarkupIsAFormatError(string markup)
    {
        Assert.Throws<FormatException>(() => MarkupFragment.Parse(markup));
    }

    [Fact]
    public void AFoldedStyleMakesNoElementAndAddsItsSheetOnce()
    {
        Handwritten written = Mount<Handwritten>(w => w.Markup = "<style>.a { color: red; }</style>");

        Assert.Equal(
            [
                "CreateElement(#1, button, -1) = #2",
                $"AddStylesheet({typeof(Handwritten).FullName}.razor <style> at sequence 4, " +
                $"{ComponentScope.ClassOf(typeof(Handwritten))}): .a {{ color: red; }}",
            ],
            Frame());
        Assert.Empty(Rebuild(written));
    }

    /// <summary>Loose text in two parts under the host, as <c>@First@Second</c> generates it.</summary>
    private sealed class TwoParts : UIComponent
    {
        public string First = "";
        public string Second = "b";

        protected override void BuildRenderTree(RenderTreeBuilder __builder)
        {
            __builder.AddContent(0, First);
            __builder.AddContent(1, Second);
        }
    }

    /// <summary>A Razor component written as C#, for what generated markup cannot do: a tag that changes.</summary>
    private sealed class Handwritten : UIComponent
    {
        public string Tag = "button";
        public bool Disabled;
        public string? Tooltip;
        public string? Id;
        public string? Markup;

        protected override void BuildRenderTree(RenderTreeBuilder __builder)
        {
            __builder.OpenElement(0, Tag);
            __builder.AddAttribute(1, "disabled", Disabled);
            __builder.AddAttribute(2, "tooltip", Tooltip);
            __builder.AddAttribute(3, "id", Id);
            __builder.CloseElement();
            __builder.AddMarkupContent(4, Markup);
        }
    }
}
