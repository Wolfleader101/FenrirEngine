using Fenrir.Scripting;
using System.Runtime.CompilerServices;
using Xunit;

namespace Fenrir.UI.Razor.Tests;

// Child Razor components, fragments and references through the recording fake of the element seam (spec: "The
// builder" child components and references, "Components, mounting and the dispatcher"; ticket 05): a child rebuilds
// only when a parameter changed, its elements keep their places among its parent's, and @ref follows its element.
public sealed class ChildComponentTests : IDisposable
{
    private static readonly ElementId Parent = RecordingSeam.Id(100);

    private readonly RecordingSeam seam = new();
    private readonly List<string> errors = [];

    public ChildComponentTests()
    {
        RazorRuntime.Release();
        RazorDiagnostics.Sink = (error, message) =>
        {
            if (error)
                errors.Add(message);
        };
    }

    public void Dispose()
    {
        RazorRuntime.Release();
        RazorDiagnostics.Sink = null;
    }

    private T Mount<T>(Action<T>? init = null) where T : UIComponent, new() =>
        (T)RazorRuntime.Mount(default, seam, Parent, -1, init).Component;

    private List<string> RunFrame()
    {
        seam.Calls.Clear();
        RazorRuntime.Build(default(SystemContext));
        List<string> calls = [.. seam.Calls];
        seam.Calls.Clear();
        Assert.Empty(errors);
        return calls;
    }

    private List<string> Rebuild(UIComponent component)
    {
        component.StateHasChanged();
        return RunFrame();
    }

    // ---- Parameters ----

    [Fact]
    public void AChildsElementsGoUnderTheEnclosingElementInMarkupOrder()
    {
        Mount<Pair>();
        Assert.Equal(
            [
                "CreateElement(#1, panel, -1) = #2",
                "AddClass(#2, pair)",
                "CreateElement(#2, label, -1) = #3",
                "AddClass(#3, before)",
                "SetText(#3, b)",
                "CreateElement(#2, label, -1) = #4",
                "AddClass(#4, badge)",
                "SetStyle(#4, width, 10px)",
                "SetText(#4, one)",
                "CreateElement(#2, label, -1) = #5",
                "AddClass(#5, badge)",
                "SetStyle(#5, width, 10px)",
                "SetText(#5, fixed)",
                "CreateElement(#2, label, -1) = #6",
                "AddClass(#6, after)",
                "SetText(#6, a)",
            ],
            RunFrame());
    }

    [Fact]
    public void AChildsParameterChangeCostsOnlyItsChangedValues()
    {
        Pair pair = Mount<Pair>();
        RunFrame();

        pair.Title = "two";
        Assert.Equal(["SetText(#4, two)"], Rebuild(pair));

        pair.Width = 20;
        Assert.Equal(["SetStyle(#4, width, 20px)"], Rebuild(pair));

        Assert.Equal(["mount", "parameters one", "built first", "parameters two", "built", "parameters two", "built"],
                     pair.First!.Hooks);
        Assert.Equal(["mount", "parameters fixed", "built first"], pair.Fixed!.Hooks);
    }

    [Fact]
    public void AnUnchangedParameterRebuildsNothing()
    {
        Pair pair = Mount<Pair>();
        RunFrame();

        Assert.Empty(Rebuild(pair));

        pair.After = "z";
        Assert.Equal(["SetText(#6, z)"], Rebuild(pair));
        Assert.Equal(["#3", "#4", "#5", "#6"], seam.ChildrenOf(RecordingSeam.Id(2)));
        Assert.Equal(["mount", "parameters one", "built first"], pair.First!.Hooks);
        Assert.Equal(["mount", "parameters fixed", "built first"], pair.Fixed!.Hooks);
    }

    [Fact]
    public void AChildNoLongerBuiltIsRemovedAndUnmountedAndANewOneTakesItsPlace()
    {
        Pair pair = Mount<Pair>();
        RunFrame();
        Badge first = pair.First!;

        pair.ShowFirst = false;
        Assert.Equal(["Remove(#4, Outro)"], Rebuild(pair));
        Assert.Equal("unmount", first.Hooks[^1]);
        Assert.Null(pair.First);

        seam.FinishOutros();
        pair.ShowFirst = true;
        Rebuild(pair);
        Assert.NotSame(first, pair.First);
        Assert.Equal(["mount", "parameters one", "built first"], pair.First!.Hooks);
        Assert.Equal(["#3", "#7", "#5", "#6"], seam.ChildrenOf(RecordingSeam.Id(2)));
    }

    [Fact]
    public void StateHasChangedOnAChildRebuildsItAlone()
    {
        Pair pair = Mount<Pair>();
        RunFrame();

        pair.Fixed!.Suffix = "!";
        Assert.Equal(["SetText(#5, fixed!)"], Rebuild(pair.Fixed));
        Assert.Equal(["mount", "parameters fixed", "built first", "built"], pair.Fixed.Hooks);
        Assert.Equal(["mount", "parameters one", "built first"], pair.First!.Hooks);
    }

    [Fact]
    public void AChildsChangedBuildHashRebuildsItOnce()
    {
        Pair pair = Mount<Pair>();
        RunFrame();

        pair.First!.Hash = 3;
        Assert.Empty(RunFrame());
        Assert.Empty(RunFrame());
        Assert.Equal(["mount", "parameters one", "built first", "built"], pair.First.Hooks);
    }

    [Fact]
    public void AHashItsNewParametersChangedAsksForNoSecondBuild()
    {
        Pair pair = Mount<Pair>();
        RunFrame();
        pair.First!.HashFromText = true;
        RunFrame();
        int builds = pair.First.Hooks.Count(hook => hook == "built");

        pair.Title = "longer";
        Rebuild(pair);
        RunFrame();

        Assert.Equal(builds + 1, pair.First.Hooks.Count(hook => hook == "built"));
    }

    [Fact]
    public void AChildsTextInALabelIsKeptWhenItsParentRebuildsForSomethingElse()
    {
        Sentence sentence = Mount<Sentence>();
        Assert.Contains("SetText(#2, Score: ten)", RunFrame());

        sentence.Other = "p";
        Assert.Equal(["SetText(#3, p)"], Rebuild(sentence));

        sentence.Word = "nine";
        Assert.Equal(["SetText(#2, Score: nine)"], Rebuild(sentence));
    }

    [Fact]
    public void AThrowingOnUnmountIsLoggedAndTheOtherChildrenAreStillForgotten()
    {
        Pair pair = Mount<Pair>();
        RunFrame();
        Badge first = pair.First!;
        Badge @fixed = pair.Fixed!;
        first.ThrowOnUnmount = true;

        RazorRuntime.Release();

        Assert.Single(errors);
        errors.Clear();
        Assert.Equal("unmount", @fixed.Hooks[^1]);
        Assert.Null(pair.Fixed);
    }

    [Fact]
    public void AKeyLeftByABuildThatFailedWhileForgettingIsNotHeldAfterRelease()
    {
        WeakReference key = FailABuildWhileForgettingAKey();

        RazorRuntime.Release();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        // A @key can be an object of a script generation's type, which would keep the generation from being collected.
        Assert.False(key.IsAlive);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private WeakReference FailABuildWhileForgettingAKey()
    {
        object key = new();
        ObjectKeyed keyed = Mount<ObjectKeyed>(k => k.Keys.Add(key));
        RunFrame();

        keyed.Keys.Clear();
        keyed.StateHasChanged();
        RazorRuntime.Build(default(SystemContext));
        Assert.Single(errors);
        errors.Clear();
        return new WeakReference(key);
    }

    [Fact]
    public void ReleaseForgetsChildRazorComponents()
    {
        Pair pair = Mount<Pair>();
        RunFrame();
        Badge first = pair.First!;
        Badge @fixed = pair.Fixed!;

        RazorRuntime.Release();

        Assert.Equal("unmount", first.Hooks[^1]);
        Assert.Equal("unmount", @fixed.Hooks[^1]);
    }

    [Fact]
    public void KeyedChildRazorComponentsReversedAreOneReorderAndDoNotBuild()
    {
        BadgeList list = Mount<BadgeList>(l => l.Names = ["a", "b"]);
        RunFrame();

        list.Names = ["b", "a"];
        Assert.Equal(["Reorder(#4, 0)"], Rebuild(list));
        Assert.Equal(["#4", "#3"], seam.ChildrenOf(RecordingSeam.Id(2)));
        Assert.Empty(Rebuild(list));
    }

    // ---- Fragments ----

    [Fact]
    public void AChildContentFragmentsElementsKeepTheirKeysAcrossParentRebuilds()
    {
        Framed framed = Mount<Framed>();
        Assert.Equal(
            [
                "CreateElement(#1, panel, -1) = #2",
                "AddClass(#2, shell)",
                "CreateElement(#2, label, -1) = #3",
                "AddClass(#3, inner)",
                "SetText(#3, in)",
                "CreateElement(#1, label, -1) = #4",
                "AddClass(#4, outside)",
                "SetText(#4, out)",
            ],
            RunFrame());

        Assert.Empty(Rebuild(framed));

        framed.Inner = "x";
        Assert.Equal(["SetText(#3, x)"], Rebuild(framed));

        framed.Extra = true;
        Assert.Equal(["CreateElement(#2, label, -1) = #5", "AddClass(#5, extra)", "SetText(#5, Extra)"], Rebuild(framed));

        framed.Extra = false;
        Assert.Equal(["Remove(#5, Outro)"], Rebuild(framed));

        // A fragment counts as changed every build, so the Shell built each time its parent did.
        Assert.Equal(5, framed.Wrapper!.Builds);
    }

    [Fact]
    public void AFragmentsTextJoinsTheTextOfTheButtonItIsBuiltIn()
    {
        Captioned captioned = Mount<Captioned>();
        Assert.Equal(["CreateElement(#1, button, -1) = #2", "AddClass(#2, caption)", "SetText(#2, Go 0)"], RunFrame());

        captioned.Count = 1;
        Assert.Equal(["SetText(#2, Go 1)"], Rebuild(captioned));
    }

    // ---- References ----

    [Fact]
    public void ARefIsSetAndThenClearedWhenItsElementIsRemoved()
    {
        Refs refs = Mount<Refs>();
        RunFrame();
        Assert.Equal(RecordingSeam.Id(3), refs.BoxElement);
        Assert.True(refs.Box.HasValue);

        refs.Show = false;
        Assert.Equal(["Remove(#3, Outro)", "Remove(#5, Outro)"], Rebuild(refs));
        Assert.False(refs.Box.HasValue);
        Assert.Equal(new ElementId(), refs.BoxElement);
    }

    [Fact]
    public void ARefIsClearedWhenAnAncestorOfItsElementIsRemoved()
    {
        Refs refs = Mount<Refs>();
        RunFrame();
        Assert.True(refs.Inner.HasValue);

        refs.Show = false;
        Rebuild(refs);

        // The inner panel went natively with the box, with no call of its own, and its @ref was still handed none.
        Assert.False(refs.Inner.HasValue);
    }

    [Fact]
    public void ARazorComponentsRefIsSetAndThenClearedWhenItIsForgotten()
    {
        Refs refs = Mount<Refs>();
        RunFrame();
        Assert.Equal("ref", refs.Child!.Text);

        refs.Show = false;
        Rebuild(refs);
        Assert.Null(refs.Child);
    }
}
