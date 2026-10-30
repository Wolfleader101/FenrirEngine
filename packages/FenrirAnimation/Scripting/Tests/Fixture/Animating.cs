using Fenrir.Animation;
using Fenrir.Assets;
using Fenrir.Scripting;
using Fenrir.Skeleton;

namespace Fenrir.AnimationScriptFixture;

// Reads the animation events every frame, as game code would, and keeps what it read for the tests.
[System(Phase.Update)]
public sealed class AnimationEventRecorder : ISystem
{
    private readonly EventReader<AnimationEvent> events = new();
    private readonly EventReader<ClipFinished> finishes = new();
    private readonly EventReader<AnimationEventsDropped> drops = new();

    public void OnUpdate(in SystemContext context)
    {
        foreach (var crossed in events.Read())
            Recorded.Events.Add(crossed);
        foreach (var finished in finishes.Read())
            Recorded.Finishes.Add(finished);
        foreach (var dropped in drops.Read())
            Recorded.Drops.Add(dropped);
    }
}

// What the recorder read, for the tests to call through Iris.
public static class Recorded
{
    public static readonly List<AnimationEvent> Events = new();
    public static readonly List<ClipFinished> Finishes = new();
    public static readonly List<AnimationEventsDropped> Drops = new();

    public static int EventCount() => Events.Count;

    public static uint EventSkeleton(int index) => Events[index].Skeleton.Value;

    public static uint EventNameId(int index) => Events[index].NameId.Value;

    public static uint EventIndex(int index) => Events[index].Index;

    public static float EventTime(int index) => Events[index].Time;

    public static int FinishedCount() => Finishes.Count;
}

// The clip, playback and mask natives from a script, each a thin call the tests make through Iris.
public static class Animating
{
    private static readonly string[] Names = ["step", "", "Footstep", "ünïcödé"];

    public static uint IdOf(int name) => ClipEventId.Of(Names[name]).Value;

    private static AssetHandle<Clip> ClipOf(uint index, uint generation) => new(index, generation);

    public static float Duration(uint index, uint generation) => Clips.Duration(ClipOf(index, generation));

    public static int EventCount(uint index, uint generation) => Clips.EventCount(ClipOf(index, generation));

    public static float EventTime(uint index, uint generation, uint eventIndex) =>
        Clips.GetEvent(ClipOf(index, generation), eventIndex, out float time) ? time : float.NaN;

    public static string? EventName(uint index, uint generation, uint eventIndex) =>
        Clips.EventName(ClipOf(index, generation), eventIndex);

    // Adds an event named Names[name] at `time`: its index, or uint.MaxValue when refused.
    public static uint AddEvent(uint index, uint generation, float time, int name) =>
        Clips.AddEvent(ClipOf(index, generation), time, Names[name], out uint added) ? added : uint.MaxValue;

    public static bool RemoveEvent(uint index, uint generation, uint eventIndex) =>
        Clips.RemoveEvent(ClipOf(index, generation), eventIndex);

    public static int FindEvents(uint index, uint generation, int name) =>
        Clips.FindEvents(ClipOf(index, generation), Names[name]).Length;

    public static bool Seek(uint entity, double seconds) => Fenrir.Animation.Animation.Seek(new Entity(entity), seconds);

    public static double PlaybackTime(uint entity) => Fenrir.Animation.Animation.Playback(new Entity(entity)).Time;

    public static int PlaybackState(uint entity) => (int)Fenrir.Animation.Animation.Playback(new Entity(entity)).State;

    // Plays the clip on the skeleton entity from C#, crossfading over `fade` seconds.
    public static void Play(uint entity, uint index, uint generation, float fade)
    {
        var skeleton = new Entity(entity);
        var animator = new Animator { Clip = ClipOf(index, generation), FadeSeconds = fade, FadeCurve = FadeCurve.Linear };
        skeleton.Add(animator);
    }

    // A mask over the subtree of the bone named "Elbow", at half weight: its handle, the index in the high half.
    public static ulong MakeMask()
    {
        AssetHandle<BoneMask> mask = BoneMasks.Create();
        BoneMasks.Include(mask, "Elbow", 0.5f, MaskScope.Subtree);
        return ((ulong)mask.Index << 32) | mask.Generation;
    }

    public static bool ClearMask(uint index, uint generation) =>
        BoneMasks.Clear(new AssetHandle<BoneMask>(index, generation));
}
