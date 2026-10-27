using System.Runtime.InteropServices;

namespace Fenrir.Scripting;

/// <summary>
/// Marks a struct as an event: plain unmanaged data one system sends and others read, each through an
/// <see cref="EventReader{T}"/> of its own that yields each event once. A struct that only C# defines needs nothing else; the
/// build reports it to the engine when the script loads.
/// </summary>
public interface IEvent
{
}

/// <summary>
/// Says this struct mirrors a C++ event the engine registered under <see cref="Name"/>. The engine checks the layout against
/// the C++ type when the script loads, and C++ and C# then share one queue.
/// </summary>
[AttributeUsage(AttributeTargets.Struct, AllowMultiple = false, Inherited = false)]
public sealed class NativeEventAttribute : Attribute
{
    public NativeEventAttribute(string name) => Name = name;

    public string Name { get; }
}

/// <summary>
/// How a script assembly tells the engine which events it defines. The Fenrir.Scripting source generator writes a table that
/// calls <see cref="Report{T}"/> once for every C#-only <see cref="IEvent"/>; scripts do not call it themselves.
/// </summary>
public static unsafe class ScriptEvents
{
    public static void Report<T>() where T : unmanaged, IEvent
    {
        string name = typeof(T).FullName ?? typeof(T).Name;
        if (!Native.RegisterScriptEvent(name, sizeof(T), ScriptComponents.AlignmentOf<T>()))
            throw new InvalidOperationException($"The engine did not accept the event {name}: events are reported only while a script assembly loads, and must have a plausible size.");
    }
}

/// <summary>
/// What a system reads events of one type through. It remembers which events it has already yielded, so each event reaches it
/// once, including events sent after it last ran: the same guarantee a C++ <c>EventReader</c> gives, and independent of every
/// other reader of the queue, C# or C++. A system keeps its readers in fields (one per event type it reads), never shares them
/// with other systems, and reads with <c>foreach (var e in reader.Read())</c>. A new reader's first read (a system added
/// late, or one made by a reload) starts at the oldest event the queue still holds and reports nothing missed.
/// </summary>
public sealed unsafe class EventReader<T> where T : unmanaged, IEvent
{
    // How many events of the queue's whole history this reader has gone past (the engine's cursor). All ones until the first
    // read: the engine's NewReaderCursor.
    private ulong seen = ulong.MaxValue;

    /// <summary>
    /// The events sent since this reader last read, oldest first. Each read moves past what it yields, so the next yields only
    /// newer events. Sends made by the running system are not among them until it returns.
    /// </summary>
    /// <exception cref="InvalidOperationException">The engine does not know the event.</exception>
    public EventRange<T> Read()
    {
        // older data, older count, newer data, newer count, missed
        nuint* view = stackalloc nuint[5];
        int id = RegistryIds<T, EventKind>.Require();
        bool read;
        fixed (ulong* cursor = &seen)
            read = Native.EventReadAfter(id, cursor, view);
        if (!read)
            throw new InvalidOperationException($"Cannot read {typeof(T).Name}: the engine has no queue for it.");
        return new EventRange<T>((T*)view[0], (int)view[1], (T*)view[2], (int)view[3], (int)view[4]);
    }
}

/// <summary>
/// The events one <see cref="EventReader{T}.Read"/> yielded, oldest first. It points into the engine's queue (nothing is
/// copied), so it is a <c>ref struct</c> good for the system that read it: sends wait until the system returns, so it stays
/// valid that long.
/// </summary>
public readonly unsafe ref struct EventRange<T> where T : unmanaged, IEvent
{
    private readonly T* older;
    private readonly int olderCount;
    private readonly T* newer;

    internal EventRange(T* older, int olderCount, T* newer, int newerCount, int missed)
    {
        this.older = older;
        this.olderCount = olderCount;
        this.newer = newer;
        Count = olderCount + newerCount;
        Missed = missed;
    }

    /// <summary>How many events the read yielded.</summary>
    public int Count { get; }

    /// <summary>
    /// How many events were dropped before the reader got to them, because it went two of the queue's swaps without reading.
    /// Zero for a reader that reads every frame.
    /// </summary>
    public int Missed { get; }

    /// <summary>The event at <paramref name="index"/>, oldest first.</summary>
    public ref readonly T this[int index]
    {
        get
        {
            if ((uint)index >= (uint)Count)
                throw new ArgumentOutOfRangeException(nameof(index));
            return ref index < olderCount ? ref older[index] : ref newer[index - olderCount];
        }
    }

    public Enumerator GetEnumerator() => new(this);

    public ref struct Enumerator
    {
        private readonly EventRange<T> range;
        private int index;

        internal Enumerator(EventRange<T> range)
        {
            this.range = range;
            index = -1;
        }

        /// <summary>The current event, by readonly reference (a <c>var</c> loop variable copies it).</summary>
        public readonly ref readonly T Current => ref range[index];

        public bool MoveNext() => ++index < range.Count;
    }
}

public readonly partial struct SystemContext
{
    /// <summary>Sending events. Reading is through an <see cref="EventReader{T}"/> field.</summary>
    public EventsView Events => default;

    public readonly unsafe struct EventsView
    {
        /// <summary>
        /// Sends an event. It reaches the queue when the system returns, so the systems after it read it this frame and the
        /// sender's own readers read it the next time they read.
        /// </summary>
        /// <exception cref="InvalidOperationException">The engine does not know the event.</exception>
        public void Send<T>(in T value) where T : unmanaged, IEvent
        {
            ChangeError code;
            fixed (T* pointer = &value)
                code = Native.EventSend(RegistryIds<T, EventKind>.Require(), pointer);
            if (code != ChangeError.None)
                throw new InvalidOperationException(code == ChangeError.Unknown
                    ? $"Cannot send {typeof(T).Name}: the engine has no queue for it."
                    : $"Cannot send {typeof(T).Name}: the engine refused it ({code}).");
        }
    }
}
