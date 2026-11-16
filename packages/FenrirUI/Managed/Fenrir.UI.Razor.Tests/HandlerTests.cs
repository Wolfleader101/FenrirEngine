using System.Runtime.CompilerServices;
using Fenrir.Math;
using Fenrir.Scripting;
using Fenrir.UI.Razor.Tests.Handlers;
using Xunit;

namespace Fenrir.UI.Razor.Tests;

// Handlers and two-way binding through the recording fake of the element seam (spec: "Handlers and binding"; ADR-0069;
// ticket 06): a frame's UI events reach the nearest handler of their kind, which rebuilds its Razor component in the
// same frame, and @bind moves each side only when it changed.
public sealed class HandlerTests : IDisposable
{
    private static readonly ElementId Parent = RecordingSeam.Id(100);

    private readonly RecordingSeam seam = new();
    private readonly List<string> warnings = [];
    private readonly List<string> errors = [];

    public HandlerTests()
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

    /// <summary>One frame of the build stage with these UI events, and the native calls it made.</summary>
    private List<string> Frame(params UIEvent[] events)
    {
        seam.Calls.Clear();
        RazorRuntime.Build(default(SystemContext), events);
        List<string> calls = [.. seam.Calls];
        seam.Calls.Clear();
        return calls;
    }

    /// <summary>A UI event of the default root, as the engine would send it.</summary>
    private static UIEvent Event(UIEventKind kind, ElementId target, float x = 0, float y = 0, float value = 0)
    {
        var made = new UIEvent();
        Unsafe.AsRef(in made.Kind) = kind;
        Unsafe.AsRef(in made.Target) = target;
        Unsafe.AsRef(in made.Position) = new Vector2(x, y);
        Unsafe.AsRef(in made.Value) = value;
        return made;
    }

    private ElementId Element(string id) => seam.ElementWithId(id);

    // ---- Handlers ----

    [Fact]
    public void AClickRunsTheButtonsHandlerOnceAndItsTextChangesTheSameFrame()
    {
        Clicker clicker = Mount<Clicker>();
        Frame();

        List<string> calls = Frame(Event(UIEventKind.Click, Element("add")));

        Assert.Equal(1, clicker.Count);
        Assert.Equal(0, clicker.OuterClicks);
        Assert.Contains($"SetText({Name(Element("add"))}, Add 1)", calls);
    }

    [Fact]
    public void AnAncestorsHandlerRunsWhenNothingNearerHandlesTheKind()
    {
        Clicker clicker = Mount<Clicker>();
        Frame();

        // The label is in a panel with only a pointer-down handler, inside the outer panel's click handler.
        Frame(Event(UIEventKind.Click, Element("inner")));

        Assert.Equal(1, clicker.OuterClicks);
        Assert.Equal(0, clicker.Count);
        Assert.Null(clicker.Pressed);
    }

    [Fact]
    public void AnActionOfUIEventReceivesTheEventsPositionAndKind()
    {
        Clicker clicker = Mount<Clicker>();
        Frame();

        Frame(Event(UIEventKind.PointerPress, Element("inner"), 12, 34));

        UIEvent pressed = Assert.IsType<UIEvent>(clicker.Pressed);
        Assert.Equal(UIEventKind.PointerPress, pressed.Kind);
        Assert.Equal(12, pressed.Position.X);
        Assert.Equal(34, pressed.Position.Y);
    }

    [Fact]
    public void AnEventOfAKindNoElementHandlesRunsNothing()
    {
        Clicker clicker = Mount<Clicker>();
        Frame();
        Frame();

        Assert.Empty(Frame(Event(UIEventKind.KeyPress, Element("add"))));
        Assert.Equal(0, clicker.Count + clicker.OuterClicks);
    }

    [Fact]
    public void AThrowingHandlerTurnsItsRazorComponentOff()
    {
        Clicker clicker = Mount<Clicker>(c => c.Bad = true);
        Frame();

        Frame(Event(UIEventKind.Click, Element("bad")));
        Assert.Contains("the handler failed", Assert.Single(errors));

        clicker.StateHasChanged();
        Assert.Empty(Frame(Event(UIEventKind.Click, Element("add"))));
        Assert.Equal(0, clicker.Count);
    }

    [Fact]
    public void ARemovedElementsHandlerIsForgotten()
    {
        Clicker clicker = Mount<Clicker>(c => c.Bad = true);
        Frame();
        ElementId bad = Element("bad");

        clicker.Bad = false;
        clicker.StateHasChanged();
        Frame();
        seam.FinishOutros();

        Frame(Event(UIEventKind.Click, bad));
        Assert.Empty(errors);
    }

    [Fact]
    public void AHandlerDroppedFromTheMarkupNoLongerRuns()
    {
        Toggled toggled = Mount<Toggled>();
        Frame();

        toggled.Armed = false;
        toggled.StateHasChanged();
        Frame();

        Frame(Event(UIEventKind.Click, Element("toggled")));
        Assert.Equal(0, toggled.Clicks);
    }

    [Fact]
    public void AReleaseForgetsEveryHandler()
    {
        Clicker clicker = Mount<Clicker>();
        Frame();
        ElementId add = Element("add");

        RazorRuntime.Release();

        Frame(Event(UIEventKind.Click, add));
        Assert.Equal(0, clicker.Count);
    }

    // ---- The pointer check ----

    [Fact]
    public void EachPointerHandlersElementIsCheckedOnceAfterItsFirstLayout()
    {
        Mount<Clicker>();

        List<string> first = Frame();
        Assert.DoesNotContain(first, call => call.StartsWith("WarnIfTakesNoPointer", StringComparison.Ordinal));

        List<string> checks = Frame().Where(call => call.StartsWith("WarnIfTakesNoPointer", StringComparison.Ordinal)).ToList();
        Assert.Equal(3, checks.Count);
        Assert.Contains(checks, call => call.StartsWith($"WarnIfTakesNoPointer({Name(Element("outer"))}", StringComparison.Ordinal));
        Assert.Contains(checks, call => call.StartsWith($"WarnIfTakesNoPointer({Name(Element("add"))}", StringComparison.Ordinal));
        string pad = Assert.Single(checks, call => call.StartsWith($"WarnIfTakesNoPointer({Name(Element("pad"))}", StringComparison.Ordinal));
        Assert.Contains(typeof(Clicker).FullName!, pad);
        Assert.Contains("@onpointerdown", pad);
        Assert.Contains("pointer-events: all", pad);

        Assert.Empty(Frame());
    }

    // ---- Binding ----

    [Fact]
    public void BoundControlsArePushedOnceAndNotAgainUnchanged()
    {
        Settings settings = Mount<Settings>();

        List<string> first = Frame();

        Assert.Contains($"SetValue({Name(Element("volume"))}, 0.5)", first);
        Assert.Contains($"SetValue({Name(Element("muted"))}, 0)", first);
        Assert.Contains($"SetEntryText({Name(Element("name"))}, Ada)", first);
        Assert.Contains($"SetValue({Name(Element("loading"))}, 0.25)", first);
        settings.StateHasChanged();
        Assert.Empty(Frame());
    }

    [Fact]
    public void AFieldChangedByCodeMovesTheSliderAndNothingElse()
    {
        Settings settings = Mount<Settings>();
        Frame();
        Frame();

        settings.Volume = 0.75f;
        settings.StateHasChanged();

        Assert.Equal([$"SetValue({Name(Element("volume"))}, 0.75)"], Frame());
        Assert.Equal(0, settings.AfterCount);
    }

    [Fact]
    public void ASliderChangeWritesTheFieldRunsAfterOnceAndIsNotPushedBack()
    {
        Settings settings = Mount<Settings>();
        Frame();
        ElementId volume = Element("volume");

        // A drag's move, then its release's closing Change, which repeats the value.
        seam.UserSetsValue(volume, 0.3f);
        List<string> calls = Frame(Event(UIEventKind.Change, volume, value: 0.3f),
                                   Event(UIEventKind.Change, volume, value: 0.3f));

        Assert.Equal(0.3f, settings.Volume);
        Assert.Equal(1, settings.AfterCount);
        Assert.Contains($"GetValue({Name(volume)})", calls);
        Assert.DoesNotContain(calls, call => call.StartsWith("SetValue", StringComparison.Ordinal));
    }

    [Fact]
    public void ACheckboxAndATextEntryBindBothWays()
    {
        Settings settings = Mount<Settings>();
        Frame();
        ElementId muted = Element("muted");
        ElementId name = Element("name");

        seam.UserSetsValue(muted, 1);
        seam.UserTypes(name, "Grace");
        List<string> calls = Frame(Event(UIEventKind.Change, muted, value: 1), Event(UIEventKind.Change, name));

        Assert.True(settings.Muted);
        Assert.Equal("Grace", settings.Name);
        Assert.DoesNotContain(calls, call => call.StartsWith("Set", StringComparison.Ordinal));

        settings.Name = "Linus";
        settings.Muted = false;
        settings.StateHasChanged();
        List<string> pushed = Frame();
        Assert.Contains($"SetEntryText({Name(name)}, Linus)", pushed);
        Assert.Contains($"SetValue({Name(muted)}, 0)", pushed);
    }

    [Fact]
    public void AChildsBindValueRoundTripsThroughItsParent()
    {
        Mixer mixer = Mount<Mixer>();
        Frame();
        ElementId knob = Element("knob");
        Assert.Equal("0.2", seam.ValueOf(knob));

        seam.UserSetsValue(knob, 0.7f);
        List<string> calls = Frame(Event(UIEventKind.Change, knob, value: 0.7f));

        Assert.Equal(0.7f, mixer.Level);
        Assert.Contains($"SetText({Name(Element("level"))}, Level 0.7)", calls);
        Assert.DoesNotContain(calls, call => call.StartsWith("SetValue", StringComparison.Ordinal));

        mixer.Level = 0.1f;
        mixer.StateHasChanged();
        Assert.Contains($"SetValue({Name(knob)}, 0.1)", Frame());
    }

    private static string Name(ElementId element) => $"#{element.Index}";
}
