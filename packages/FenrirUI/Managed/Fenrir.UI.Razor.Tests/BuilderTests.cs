using System.Reflection;
using Fenrir.Scripting;
using Xunit;

[assembly: CollectionBehavior(DisableTestParallelization = true)]

namespace Fenrir.UI.Razor.Tests;

// Mounting and the first build through the recording fake of the element seam (spec: "The builder", "Components,
// mounting and the dispatcher"). RazorRuntime is static, as it is in a script: each test starts and ends released.
public sealed class BuilderTests : IDisposable
{
    private static readonly ElementId Parent = RecordingSeam.Id(100);

    private readonly RecordingSeam seam = new();

    public BuilderTests() => RazorRuntime.Release();

    public void Dispose() => RazorRuntime.Release();

    private ComponentRef<T> Mount<T>(Action<T>? init = null) where T : UIComponent, new() =>
        new(RazorRuntime.Mount(default, seam, Parent, -1, init));

    [Fact]
    public void MountingMakesTheHostWithItsReadableClassAndBuildsNothingYet()
    {
        ComponentRef<HudPanel> mounted = Mount<HudPanel>();

        Assert.Equal(["CreateElement(#100, panel, -1) = #1", "AddClass(#1, hud-panel)"], seam.Calls);
        Assert.True(mounted.IsMounted);
        Assert.Equal(1u, mounted.Host.Index);
        Assert.Equal(mounted.Host, mounted.Instance!.Element);
    }

    [Fact]
    public void TheFirstBuildMakesTheElementsWithTheirClassesStylesIdAttributesAndText()
    {
        Mount<HudPanel>(hud => hud.Health = 42);
        seam.Calls.Clear();

        RazorRuntime.Build(default(SystemContext));

        Assert.Equal(
            [
                "CreateElement(#1, panel, -1) = #2",
                "AddClass(#2, hud)",
                "AddClass(#2, calm)",
                "SetStyle(#2, width, 120px)",
                "SetStyle(#2, height, 40px)",
                "SetIdString(#2, hud)",
                "CreateElement(#2, label, -1) = #3",
                "AddClass(#3, title)",
                "SetText(#3, HP: 42)",
                "CreateElement(#2, button, -1) = #4",
                "AddClass(#4, ok)",
                "SetAttribute(#4, tooltip, Close)",
                "SetText(#4, OK)",
            ],
            seam.Calls);
    }

    [Fact]
    public void AMountedRazorComponentBuildsOnce()
    {
        Mount<HudPanel>();
        RazorRuntime.Build(default(SystemContext));
        seam.Calls.Clear();

        RazorRuntime.Build(default(SystemContext));

        Assert.Empty(seam.Calls);
    }

    [Fact]
    public void ReleaseRemovesEveryHostAtOnceAndForgetsThem()
    {
        ComponentRef<HudPanel> first = Mount<HudPanel>();
        ComponentRef<HudPanel> second = Mount<HudPanel>();
        RazorRuntime.Build(default(SystemContext));
        seam.Calls.Clear();

        RazorRuntime.Release();

        Assert.Equal(["Remove(#2, Immediate)", "Remove(#1, Immediate)"], seam.Calls);
        Assert.False(first.IsMounted);
        Assert.False(second.IsMounted);
        seam.Calls.Clear();
        RazorRuntime.Release();
        RazorRuntime.Build(default(SystemContext));
        Assert.Empty(seam.Calls);
    }

    [Fact]
    public void ReleaseBeforeTheFirstBuildDropsTheQueuedBuild()
    {
        Mount<HudPanel>();
        RazorRuntime.Release();
        seam.Calls.Clear();

        RazorRuntime.Build(default(SystemContext));

        Assert.Empty(seam.Calls);
    }

    [Fact]
    public void UnmountRemovesTheHostWithItsOutroOnce()
    {
        ComponentRef<HudPanel> mounted = Mount<HudPanel>();
        seam.Calls.Clear();

        Assert.True(mounted.Unmount());
        Assert.False(mounted.Unmount());
        RazorRuntime.Build(default(SystemContext));
        RazorRuntime.Release();

        Assert.Equal(["Remove(#1, Outro)"], seam.Calls);
        Assert.False(mounted.IsMounted);
    }

    [Fact]
    public void AThrowingInitRemovesTheHostItMade()
    {
        Assert.Throws<InvalidOperationException>(() => Mount<HudPanel>(_ => throw new InvalidOperationException()));

        Assert.Equal(["CreateElement(#100, panel, -1) = #1", "AddClass(#1, hud-panel)", "Remove(#1, Immediate)"],
                     seam.Calls);
    }

    [Fact]
    public void ADefaultReferenceNamesNothing()
    {
        ComponentRef<HudPanel> none = default;

        Assert.False(none.IsMounted);
        Assert.Null(none.Instance);
        Assert.Equal(new ElementId().Index, none.Host.Index);
        Assert.False(none.Unmount());
    }

    [Theory]
    [InlineData(typeof(HudPanel), "hud-panel")]
    [InlineData(typeof(HUDPanel), "hud-panel")]
    [InlineData(typeof(Slot2Grid), "slot2-grid")]
    [InlineData(typeof(Wrapper<int>), "wrapper")]
    public void TheReadableClassIsTheTypeNameInKebabCase(Type type, string expected)
    {
        Assert.Equal(expected, RazorRuntime.ReadableClass(type));
    }

    [Fact]
    public void TheOnlyAspNetCoreAssemblyReferencedIsTheStandIn()
    {
        AssemblyName[] aspNetCore = typeof(HudPanel).Assembly.GetReferencedAssemblies()
                                        .Where(name => name.Name!.StartsWith("Microsoft.AspNetCore.", StringComparison.Ordinal))
                                        .ToArray();

        AssemblyName standIn = Assert.Single(aspNetCore);
        Assert.Equal("Microsoft.AspNetCore.Components", standIn.Name);
        Assert.Equal(typeof(Microsoft.AspNetCore.Components.ComponentBase).Assembly.GetName().Version, standIn.Version);
        Assert.Equal(new Version(0, 1, 0, 0), standIn.Version);
    }

    private sealed class HUDPanel;

    private sealed class Slot2Grid;

    private sealed class Wrapper<T>;
}
