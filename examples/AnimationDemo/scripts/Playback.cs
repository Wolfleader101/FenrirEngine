using Fenrir.Animation;
using Fenrir.Assets;
using Fenrir.Demo;
using Fenrir.DebugDraw;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;
using Fenrir.Skeleton;

// Outside any Fenrir.* namespace, as a game's scripts are, so `Assets` and `Animation` name the classes.
namespace AnimationDemo.Scripts;

// PLAYBACK (C#): SCENE 1'S PLAYBACK, DRIVEN FROM C#
//
// The scene after "Crowd" in AnimationDemo's list. The scaffold gives its FenrirScene a CSharpScene resource naming it
// (src/CSharpScenes.cpp, PlaybackCSharpScene) and the floor and sun; the Quaternius human and everything it plays is
// made here, through Renderer.SpawnModel (which gives it its SkeletonPose), the Animator and an AnimationLayer:
//
//   1-5        crossfade to Idle, Walk, Run, Sprint or Call over 0.4 s (Animator.FadeSeconds).
//   Up/Down    speed. R reverses. L switches Loop and Once. Space pauses.
//   Left/Right seek 0.25 s (Animation.Seek). Home seeks to 0.
//   M          ramps the masked layer in or out: the call on the upper body alone, an AnimationLayer whose BoneMask
//              covers spine_01's subtree, made with BoneMasks.
//
// Its clip events (authored here with Clips.AddEvent) are read through EventReader<AnimationEvent> and shown above the
// human's head; ClipFinished shows as a message. The screen text is Animation.Playback's: the clip, its time, the speed,
// the loop mode, laps, and the fade's progress. Every system acts only while the scene is active.

// The scene's state, on one entity: what is made, what plays and what the keys set.
public struct PlaybackScene : IComponent
{
    public int Built; // 0 nothing yet, 1 the human spawned, 2 its skeleton found and playing.
    public Entity Root;
    public Entity Skeleton;
    public Entity Layer;
    public AssetHandle<Clip> Idle, Walk, Run, Sprint, Call;
    public int Clip;
    public float Speed;
    public ClipLoop Loop;
    public bool Playing;
    public bool LayerOn;
    public float LayerWeight;
}

public static class PlaybackCSharp
{
    // This scene's number in its CSharpScene, as C++ gives it (PlaybackCSharpScene in src/AnimationScenes.hpp).
    public const uint SceneId = 1;

    public const float FadeSeconds = 0.4f;
    public const float SpeedStep = 0.25f;
    public const double SeekStep = 0.25;
    public const float LayerRamp = 2.0f; // Weight per second.

    public static readonly string[] ClipNames = ["Idle", "Walk", "Run", "Sprint", "Call"];
    public static readonly Key[] ClipKeys = [Key.Num1, Key.Num2, Key.Num3, Key.Num4, Key.Num5];

    public static AssetHandle<Clip> ClipOf(in PlaybackScene state, int clip) => clip switch
    {
        0 => state.Idle,
        1 => state.Walk,
        2 => state.Run,
        3 => state.Sprint,
        _ => state.Call,
    };
}

// Builds the scene once each time it is entered, in two frames: the human first, alive at once but given its
// SkeletonPose when this system returns; then, the next frame, the Animator on its skeleton entity and the masked layer.
[System(Phase.Update, Order = 100)]
public sealed class PlaybackBuilder : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!CSharpScene.IsActive(PlaybackCSharp.SceneId, out _))
            return;
        foreach (var (_, state) in context.Query<PlaybackScene>())
        {
            if (state.Value.Built == 1)
                Start(in context, ref state.Value);
            return;
        }

        var scene = new PlaybackScene { Speed = 1, Loop = ClipLoop.Loop, Playing = true, Clip = 1 };
        AssetHandle<Model> human = Assets.Load<Model>("quaternius/rig-human.glb");
        scene.Root = Renderer.SpawnModel(human);
        if (scene.Root == Entity.None)
        {
            context.Log.Error("Playback (C#): quaternius/rig-human.glb did not spawn");
            return;
        }
        scene.Idle = Assets.Load<Clip>("quaternius/idle_loop.glb#Clip0");
        scene.Walk = Assets.Load<Clip>("quaternius/walk_loop.glb#Clip0");
        scene.Run = Assets.Load<Clip>("quaternius/jog_fwd_loop.glb#Clip0");
        scene.Sprint = Assets.Load<Clip>("quaternius/sprint_loop.glb#Clip0");
        scene.Call = Assets.Load<Clip>("quaternius/idle_rail_call.glb#Clip0");
        AddEvents(scene);
        scene.Built = 1;
        Entity holder = Entity.Create();
        var name = new Name();
        name.SetText("Playback (C#) scene");
        holder.Add(name);
        holder.Add(scene);
    }

    // Names moments of each clip as events, at fractions of its duration, unless an earlier entry already did.
    private static void AddEvents(in PlaybackScene scene)
    {
        void Add(AssetHandle<Clip> clip, float fraction, string name)
        {
            if (Clips.FindEvents(clip, name).Length == 0)
                Clips.AddEvent(clip, Clips.Duration(clip) * fraction, name, out _);
        }
        Add(scene.Idle, 0.5f, "C# Breathe");
        Add(scene.Walk, 0.0f, "C# Step.A");
        Add(scene.Walk, 0.5f, "C# Step.B");
        Add(scene.Run, 0.0f, "C# Stride.A");
        Add(scene.Run, 0.5f, "C# Stride.B");
        Add(scene.Sprint, 0.0f, "C# Dash");
        Add(scene.Call, 0.3f, "C# Hey!");
    }

    // The skeleton entity is the one entity of the scene with a SkeletonPose: the human's owner.
    private static void Start(in SystemContext context, ref PlaybackScene state)
    {
        foreach (var (entity, _) in context.Query<SkeletonPose>())
            state.Skeleton = entity;
        if (state.Skeleton == Entity.None)
        {
            context.Log.Error("Playback (C#): the human spawned no skeleton entity");
            state.Built = 3;
            return;
        }
        state.Skeleton.Add(new Animator
        {
            Clip = state.Walk,
            FadeSeconds = PlaybackCSharp.FadeSeconds,
            FadeCurve = FadeCurve.Smooth,
        });

        // The upper body's call, at weight 0 until M ramps it in: one masked layer over whatever the Animator plays.
        AssetHandle<BoneMask> upper = BoneMasks.Create();
        BoneMasks.Include(upper, "spine_01", 1.0f, MaskScope.Subtree);
        state.Layer = Entity.Create();
        var name = new Name();
        name.SetText("Upper-body layer");
        state.Layer.Add(name);
        state.Layer.Add(new AnimationLayer { Skeleton = state.Skeleton, Clip = state.Call, Mask = upper, Weight = 0 });
        state.Built = 2;
        context.Log.Info("Playback (C#): the human walks; 1-5 crossfade, M ramps the upper-body layer");
    }
}

// The keys, each frame, written to the Animator and the layer.
[System(Phase.Update, Order = 200)]
public sealed class PlaybackKeys : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!CSharpScene.IsActive(PlaybackCSharp.SceneId, out _))
            return;
        foreach (var (_, state) in context.Query<PlaybackScene>())
        {
            if (state.Value.Built == 2)
                Handle(in context, ref state.Value);
            return;
        }
    }

    private static void Handle(in SystemContext context, ref PlaybackScene state)
    {
        for (int i = 0; i < PlaybackCSharp.ClipKeys.Length; ++i)
        {
            if (!Keyboard.JustPressed(PlaybackCSharp.ClipKeys[i]))
                continue;
            state.Clip = i;
            context.Log.Info($"Playback (C#): {i + 1} crossfades to {PlaybackCSharp.ClipNames[i]}");
        }
        if (Keyboard.JustPressed(Key.Up) || Keyboard.JustPressed(Key.Down))
        {
            float magnitude = MathF.Max(0, MathF.Abs(state.Speed) +
                                           (Keyboard.JustPressed(Key.Up) ? PlaybackCSharp.SpeedStep : -PlaybackCSharp.SpeedStep));
            state.Speed = MathF.CopySign(magnitude, state.Speed);
            context.Log.Info($"Playback (C#): speed {state.Speed:0.00}");
        }
        if (Keyboard.JustPressed(Key.R))
        {
            state.Speed = -state.Speed;
            context.Log.Info($"Playback (C#): R reverses, speed {state.Speed:0.00}");
        }
        if (Keyboard.JustPressed(Key.L))
        {
            state.Loop = state.Loop == ClipLoop.Loop ? ClipLoop.Once : ClipLoop.Loop;
            context.Log.Info($"Playback (C#): L, {state.Loop}");
        }
        if (Keyboard.JustPressed(Key.Space))
        {
            state.Playing = !state.Playing;
            context.Log.Info(state.Playing ? "Playback (C#): Space, playing" : "Playback (C#): Space, paused");
        }
        if (Keyboard.JustPressed(Key.Left) || Keyboard.JustPressed(Key.Right) || Keyboard.JustPressed(Key.Home))
        {
            double now = Fenrir.Animation.Animation.Playback(state.Skeleton).Time;
            double to = Keyboard.JustPressed(Key.Home) ? 0
                        : Keyboard.JustPressed(Key.Left) ? now - PlaybackCSharp.SeekStep
                                                         : now + PlaybackCSharp.SeekStep;
            Fenrir.Animation.Animation.Seek(state.Skeleton, System.Math.Max(0, to));
            context.Log.Info($"Playback (C#): seek to {System.Math.Max(0, to):0.00} s");
        }
        if (Keyboard.JustPressed(Key.M))
        {
            state.LayerOn = !state.LayerOn;
            context.Log.Info(state.LayerOn ? "Playback (C#): M ramps the upper-body call in"
                                           : "Playback (C#): M ramps the upper-body call out");
        }
        float step = PlaybackCSharp.LayerRamp * Time.DeltaTime;
        state.LayerWeight = System.Math.Clamp(state.LayerWeight + (state.LayerOn ? step : -step), 0, 1);

        ref Animator animator = ref state.Skeleton.Get<Animator>().Value;
        animator.Clip = PlaybackCSharp.ClipOf(state, state.Clip);
        animator.Speed = state.Speed;
        animator.Loop = state.Loop;
        animator.Playing = state.Playing;
        state.Layer.Get<AnimationLayer>().Value.Weight = state.LayerWeight;
    }
}

// The events and the playback state, shown each frame.
[System(Phase.Update, Order = 300)]
public sealed class PlaybackShow : ISystem
{
    private readonly EventReader<AnimationEvent> events = new();
    private readonly EventReader<ClipFinished> finishes = new();

    public void OnUpdate(in SystemContext context)
    {
        // Read every frame, so a reader never holds events from before the scene was entered.
        var crossed = events.Read();
        var finished = finishes.Read();
        if (!CSharpScene.IsActive(PlaybackCSharp.SceneId, out _))
            return;
        foreach (var (_, state) in context.Query<PlaybackScene>())
        {
            if (state.Value.Built == 2)
                Show(in context, state.Value, crossed, finished);
            return;
        }
    }

    private static void Show(in SystemContext context, in PlaybackScene state, EventRange<AnimationEvent> crossed,
                             EventRange<ClipFinished> finished)
    {
        foreach (AnimationEvent crossing in crossed)
        {
            if (crossing.Skeleton != state.Skeleton)
                continue;
            string name = Clips.EventName(crossing.Clip, crossing.Index);
            DebugDraw.WorldText(new Vector3(0, 2.1f, 0), $"{name} (lap {crossing.Loop})", Colour.FromSrgb(0x80FF80FF),
                                new TextOptions { Duration = 1, RelativeTo = state.Skeleton, Size = 20 });
            context.Log.Info($"Playback (C#): event {name} at {crossing.Time:0.00} s, lap {crossing.Loop}");
        }
        foreach (ClipFinished done in finished)
        {
            if (done.Skeleton != state.Skeleton)
                continue;
            DebugDraw.Message("ClipFinished (C#): the Once clip reached its end and holds there",
                              Colour.FromSrgb(0xFFC060FF), new TextOptions { Duration = 2 });
            context.Log.Info("Playback (C#): ClipFinished");
        }

        PlaybackInfo info = Fenrir.Animation.Animation.Playback(state.Skeleton);
        AssetHandle<Clip> clip = PlaybackCSharp.ClipOf(state, state.Clip);
        string fade = info.Fading ? $", crossfading {info.FadeProgress:0.00}" : "";
        DebugDraw.ScreenText(new Vector2(16, 120),
                             $"C# walker: {PlaybackCSharp.ClipNames[state.Clip]} {info.Time:0.00} / " +
                             $"{Clips.Duration(clip):0.00} s, speed {state.Speed:0.00}, {state.Loop}, " +
                             $"{(state.Playing ? "playing" : "paused")}{(info.Finished ? ", finished" : "")}, laps " +
                             $"{info.CompletedLoops}{fade}",
                             Colour.White);
        DebugDraw.ScreenText(new Vector2(16, 140),
                             $"Upper-body call layer (spine_01's subtree): weight {state.LayerWeight:0.00}",
                             Colour.White);
    }
}
