using Fenrir.Scripting;
using Xunit;

namespace Fenrir.UI.Razor.Tests;

// Paint overrides through the recording fake of the element seam (spec: "Components, mounting and the dispatcher";
// ADR-0068; ticket 08): RazorRuntime.Paint, the paint stage's dispatcher call, hands each Razor component whose type
// overrides OnPaint the painter and boxes of its root element, and calls no native for one that does not.
public sealed class PaintTests : IDisposable
{
    private static readonly ElementId Parent = RecordingSeam.Id(100);

    private readonly RecordingSeam seam = new();
    private readonly List<string> errors = [];

    public PaintTests()
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

    private ComponentRef<T> Mount<T>(Action<T>? init = null) where T : UIComponent, new() =>
        new(RazorRuntime.Mount(default, seam, Parent, -1, init));

    private void Build()
    {
        RazorRuntime.Build(default(SystemContext));
        seam.Calls.Clear();
    }

    private List<string> Paint()
    {
        seam.Calls.Clear();
        RazorRuntime.Paint(default(SystemContext));
        List<string> calls = [.. seam.Calls];
        seam.Calls.Clear();
        return calls;
    }

    [Fact]
    public void AnOverrideIsHandedItsHostsPainterAndBoxes()
    {
        Painted painted = Mount<Painted>().Instance!;
        Build();

        Assert.Equal(["PaintInto(#1)", "GetBoxes(#1)"], Paint());
        Assert.Equal([1001UL], painted.Painters);
        Assert.Equal([1f], painted.BorderX);
        Assert.Empty(errors);
    }

    [Fact]
    public void ARazorComponentWithoutTheOverrideCallsNoNative()
    {
        Mount<Readout>();
        Build();

        Assert.Empty(Paint());
    }

    [Fact]
    public void ARazorComponentIsNotPaintedBeforeItsFirstBuild()
    {
        Painted painted = Mount<Painted>().Instance!;

        Assert.Empty(Paint());
        Assert.Empty(painted.Painters);
    }

    [Fact]
    public void AChildPaintsIntoItsFirstTopLevelElementAfterItsParent()
    {
        PaintedPair pair = Mount<PaintedPair>().Instance!;
        Build();

        // #2 the pair's panel, #3 its own label, #4 and #5 the two children's labels.
        Assert.Equal(["PaintInto(#1)", "GetBoxes(#1)", "PaintInto(#4)", "GetBoxes(#4)", "PaintInto(#5)", "GetBoxes(#5)"],
                     Paint());
        Assert.Equal([1001UL], pair.Painters);
        Assert.Equal([1004UL], pair.First!.Painters);
        Assert.Equal([1005UL], pair.Second!.Painters);
    }

    [Fact]
    public void NoPainterMeansNoOverrideCall()
    {
        Painted painted = Mount<Painted>().Instance!;
        Build();
        seam.DestroyRoot();

        Assert.Equal(["PaintInto(#1)"], Paint());
        Assert.Empty(painted.Painters);
    }

    [Fact]
    public void AnUnmountedRazorComponentIsNotPainted()
    {
        ComponentRef<Painted> mounted = Mount<Painted>();
        Build();
        mounted.Unmount();

        Assert.Empty(Paint());
    }

    [Fact]
    public void AThrowingOverrideIsLoggedAndTurnsItsRazorComponentOff()
    {
        ComponentRef<Painted> mounted = Mount<Painted>();
        Build();
        mounted.Instance!.Throw = true;

        Paint();
        Assert.Single(errors);
        Assert.Contains("Painted", errors[0]);
        Assert.Contains("OnPaint threw", errors[0]);

        mounted.Instance.Throw = false;
        Assert.Empty(Paint());
        mounted.Instance.StateHasChanged();
        RazorRuntime.Build(default(SystemContext));
        Assert.Empty(seam.Calls);
    }

    [Fact]
    public void AReleasedRuntimeFindsTheOverridesAgain()
    {
        Mount<Painted>();
        Build();
        Paint();
        RazorRuntime.Release();

        Painted again = Mount<Painted>().Instance!;
        Build();
        Assert.Equal(2, Paint().Count);
        Assert.Single(again.Painters);
    }
}
