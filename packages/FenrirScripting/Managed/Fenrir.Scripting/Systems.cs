namespace Fenrir.Scripting;

/// <summary>The part of the frame a system runs in, in the order they happen.</summary>
public enum Phase
{
    /// <summary>Once a frame, before the fixed steps.</summary>
    PreUpdate = 0,

    /// <summary>Once per fixed step: zero, one or several times a frame, at <see cref="Time.TickRate"/> intervals.</summary>
    Tick = 1,

    /// <summary>Once a frame.</summary>
    Update = 2,

    /// <summary>Once a frame, after <see cref="Update"/>.</summary>
    PostUpdate = 3,
}

/// <summary>
/// Marks an <see cref="ISystem"/> class to be run by the engine, in a phase and (within it) in <see cref="Order"/>, lowest
/// first. A class that implements <see cref="ISystem"/> without this attribute never runs.
/// </summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class SystemAttribute : Attribute
{
    public SystemAttribute(Phase phase) => Phase = phase;

    public Phase Phase { get; }

    /// <summary>Lowest runs first. Systems with the same order run in the order of their full names.</summary>
    public int Order { get; set; }
}

/// <summary>
/// Marks an <see cref="ISystem"/> class to be run inside an extension stage: a named part of an engine package's own work
/// (<c>Fenrir.Skeleton.Modify</c>), which the package runs at <see cref="Order"/> among its C++ work, lowest first. At an
/// order C++ work also has, the C++ work runs first. A class has this or <see cref="SystemAttribute"/>, never both.
/// <para>
/// A script naming a stage no loaded extension declares fails to load, with the system and the stage named. A stage the
/// engine never runs (its package is not installed, its scene is inactive) is not an error: the system simply does not run.
/// Structural changes a stage system makes take effect when the package's work returns, not when the system does, so the
/// systems after it in the stage do not see them. A throw drops what it changed and disables it until the next reload.
/// </para>
/// </summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class StageSystemAttribute : Attribute
{
    public StageSystemAttribute(string stage) => Stage = stage;

    /// <summary>The stage's full name, as the extension that hosts it declares it.</summary>
    public string Stage { get; }

    /// <summary>Lowest runs first. Systems with the same order run in the order of their full names.</summary>
    public int Order { get; set; }
}

/// <summary>
/// Logic that runs once per frame phase, or inside an extension stage. The engine makes one instance when the script assembly
/// loads. It needs a public or internal constructor with no parameters and a <see cref="SystemAttribute"/> or a
/// <see cref="StageSystemAttribute"/>. Structural changes it makes
/// (<see cref="Entity.Create"/>, <see cref="Entity.Destroy"/>, <see cref="Entity.Add{T}()"/>, <see cref="Entity.Remove{T}"/>) take
/// effect when the call returns, so the next system sees them (a stage system's when its stage's host returns). If a call
/// throws, the exception is logged with the system's name, the changes it made are dropped, and the system stops running
/// until the next reload; the others keep going.
/// </summary>
public interface ISystem
{
    /// <summary>Runs in the system's phase, every time the phase runs.</summary>
    void OnUpdate(in SystemContext context);

    /// <summary>Runs once, after the script assembly loads.</summary>
    void OnCreate(in SystemContext context)
    {
    }

    /// <summary>Runs once, before the script assembly is released or the engine shuts down.</summary>
    void OnDestroy(in SystemContext context)
    {
    }
}

/// <summary>What a system can reach: the clock, queries, entity creation and the log.</summary>
public readonly partial struct SystemContext
{
    /// <summary>The engine's clock.</summary>
    public TimeView Time => default;

    /// <summary>The engine's logger.</summary>
    public LogView Log => default;

    /// <summary>A new entity, usable at once; see <see cref="Entity.Create"/>.</summary>
    public Entity CreateEntity() => Entity.Create();

    public readonly struct TimeView
    {
        /// <summary>Seconds the last frame took.</summary>
        public float DeltaTime => Fenrir.Scripting.Time.DeltaTime;

        /// <summary>Seconds between fixed steps.</summary>
        public float TickRate => Fenrir.Scripting.Time.TickRate;
    }

    public readonly struct LogView
    {
        public void Info(string? message) => Fenrir.Scripting.Log.Info(message);

        public void Warn(string? message) => Fenrir.Scripting.Log.Warn(message);

        public void Error(string? message) => Fenrir.Scripting.Log.Error(message);
    }
}

/// <summary>A system the generator found in the script assembly: what the runner needs to make and place it.</summary>
public readonly struct SystemRegistration
{
    public SystemRegistration(string name, Phase phase, int order, Func<ISystem> create)
    {
        Name = name;
        Phase = phase;
        Stage = null;
        Order = order;
        Create = create;
    }

    /// <summary>A stage system: one that joins the extension stage <paramref name="stage"/> at <paramref name="order"/>.</summary>
    public SystemRegistration(string name, string stage, int order, Func<ISystem> create)
    {
        Name = name;
        Phase = default;
        Stage = stage;
        Order = order;
        Create = create;
    }

    public string Name { get; }

    /// <summary>The phase of a phase system; meaningless for a stage system.</summary>
    public Phase Phase { get; }

    /// <summary>The extension stage a stage system joins, or null for a phase system.</summary>
    public string? Stage { get; }

    public int Order { get; }

    public Func<ISystem> Create { get; }
}

/// <summary>
/// Runs a script assembly's systems for the engine. The engine makes one (through the generated
/// <c>Fenrir.Scripting.Generated.SystemHost</c>) and calls it once per phase, and once per stage group where the extension
/// hosting the stage runs it, so a phase or a group costs one transition however many systems it has. Scripts do not use it
/// directly.
/// </summary>
public sealed class SystemRunner
{
    private sealed class Entry
    {
        public required string Name { get; init; }
        public required Phase Phase { get; init; }
        public string? Stage { get; init; }
        public required int Order { get; init; }
        public int Group { get; set; } = -1; // A stage system's (stage, order) group; -1 for a phase system.
        public ISystem? Instance { get; set; }
    }

    private readonly Entry[] entries;

    // Each stage group's systems, in name order, by group number.
    private readonly Entry[][] groups;

    public SystemRunner(SystemRegistration[] registrations)
    {
        var sorted = new List<Entry>(registrations.Length);
        foreach (SystemRegistration registration in registrations)
        {
            var entry = new Entry
            {
                Name = registration.Name,
                Phase = registration.Phase,
                Stage = registration.Stage,
                Order = registration.Order,
            };
            try
            {
                entry.Instance = registration.Create();
            }
            catch (Exception exception)
            {
                Fail(entry, "its constructor", exception);
            }
            sorted.Add(entry);
        }
        // Phase systems first, by phase; then stage systems, by stage. Within either, by order, then by name.
        sorted.Sort(static (left, right) =>
        {
            int byKind = (left.Stage != null).CompareTo(right.Stage != null);
            if (byKind != 0)
                return byKind;
            int byPlace = left.Stage != null ? string.CompareOrdinal(left.Stage, right.Stage) : left.Phase.CompareTo(right.Phase);
            if (byPlace != 0)
                return byPlace;
            int byOrder = left.Order.CompareTo(right.Order);
            return byOrder != 0 ? byOrder : string.CompareOrdinal(left.Name, right.Name);
        });
        entries = sorted.ToArray();

        var grouped = new List<List<Entry>>();
        Entry? previous = null;
        foreach (Entry entry in entries)
        {
            if (entry.Stage == null)
                continue;
            if (previous == null || previous.Stage != entry.Stage || previous.Order != entry.Order)
                grouped.Add(new List<Entry>());
            entry.Group = grouped.Count - 1;
            grouped[^1].Add(entry);
            previous = entry;
        }
        groups = grouped.Select(static group => group.ToArray()).ToArray();
    }

    /// <summary>Bit <c>1 &lt;&lt; (int)phase</c> is set for each phase that has a system, so the engine runs only those.</summary>
    public int PhaseMask()
    {
        int mask = 0;
        foreach (Entry entry in entries)
            if (entry.Stage == null && entry.Instance != null) // One whose constructor threw is already out of service.
                mask |= 1 << (int)entry.Phase;
        return mask;
    }

    /// <summary>
    /// Tells the engine of each stage system, with its stage, order and group, so it can check every stage is declared and
    /// hand each group to the extension hosting its stage. One whose constructor threw is reported too: its stage is still
    /// checked.
    /// </summary>
    public void ReportStages()
    {
        foreach (Entry entry in entries)
            if (entry.Stage != null)
                Native.RegisterStageSystem(entry.Name, entry.Stage, entry.Order, entry.Group);
    }

    /// <summary>
    /// Runs the systems of one stage group, in name order, without playing back what they change: the extension hosting the
    /// stage does that when its work returns. A system that throws has only what it recorded dropped. An unknown group does
    /// nothing.
    /// </summary>
    public void RunStage(int group)
    {
        if (group < 0 || group >= groups.Length)
            return;
        SystemContext context = default;
        foreach (Entry entry in groups[group])
        {
            ISystem? system = entry.Instance;
            if (system == null)
                continue;
            int mark = Native.ChangesMark();
            try
            {
                system.OnUpdate(in context);
            }
            catch (Exception exception)
            {
                Native.ChangesDiscardFrom(mark);
                Fail(entry, "OnUpdate", exception);
            }
        }
    }

    /// <summary>Calls OnCreate on each system, in phase and order.</summary>
    public void OnCreate()
    {
        SystemContext context = default;
        foreach (Entry entry in entries)
            Invoke(entry, Stage.Create, in context);
    }

    /// <summary>Runs every system of a phase, in order. An unknown phase does nothing.</summary>
    public void Run(int phase)
    {
        SystemContext context = default;
        foreach (Entry entry in entries)
            if (entry.Stage == null && (int)entry.Phase == phase)
                Invoke(entry, Stage.Update, in context);
    }

    /// <summary>Calls OnDestroy on each system that is still running, last to first.</summary>
    public void OnDestroy()
    {
        SystemContext context = default;
        for (int index = entries.Length - 1; index >= 0; index--)
        {
            Entry entry = entries[index];
            Invoke(entry, Stage.Destroy, in context);
        }
    }

    // Runs one call of one system, then plays back what it changed so the next system sees it. A throw is logged once, drops
    // what the system recorded, and takes the system out of service.
    private static unsafe void Invoke(Entry entry, Stage stage, in SystemContext context)
    {
        ISystem? system = entry.Instance;
        if (system == null)
            return;
        try
        {
            switch (stage)
            {
                case Stage.Create:
                    system.OnCreate(in context);
                    break;
                case Stage.Update:
                    system.OnUpdate(in context);
                    break;
                default:
                    system.OnDestroy(in context);
                    break;
            }
        }
        catch (Exception exception)
        {
            Native.ChangesDiscard();
            Fail(entry, stage == Stage.Create ? "OnCreate" : stage == Stage.Update ? "OnUpdate" : "OnDestroy", exception);
            return;
        }
        Native.ChangesApply();
    }

    private enum Stage
    {
        Create,
        Update,
        Destroy,
    }

    private static void Fail(Entry entry, string stage, Exception exception)
    {
        entry.Instance = null;
        Fenrir.Scripting.Log.Error($"Script system '{entry.Name}' threw in {stage} and is disabled until the next reload: {exception}");
    }
}
