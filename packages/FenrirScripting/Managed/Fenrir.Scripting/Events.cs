using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace Fenrir.Scripting;

/// <summary>
/// Marks a struct as an event: plain unmanaged data one system sends and others read. It is delivered with the engine's
/// double buffering, so an event sent in a frame can be read in that frame (by systems that run after the sender) and in the
/// next one. A struct that only C# defines needs nothing else; the build reports it to the engine when the script loads.
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

// The registry id of an event type, looked up by name and remembered while the registry it came from is current.
internal static unsafe class EventIds<T> where T : unmanaged, IEvent
{
    private static uint generation;
    private static bool resolved;
    private static int id;

    internal static string? NativeName { get; } = typeof(T).GetCustomAttribute<NativeEventAttribute>()?.Name;

    internal static string Name { get; } = NativeName ?? typeof(T).FullName ?? typeof(T).Name;

    internal static int Id
    {
        get
        {
            uint current = Native.RegistryGeneration();
            if (!resolved || generation != current)
            {
                id = Resolve();
                generation = current;
                resolved = true;
            }
            if (id == -3)
                throw new InvalidOperationException($"'{Name}' is registered for a different C# type than {typeof(T).FullName}; a native event is reached only through the type the host validated.");
            if (id < 0)
                throw new InvalidOperationException(NativeName != null
                    ? $"The engine has no event registered as '{NativeName}' (for {typeof(T).Name})."
                    : $"The engine does not know {typeof(T).Name}: an event a script defines is registered when its assembly loads, and only if the script project uses the Fenrir scripting SDK.");
            return id;
        }
    }

    private static int Resolve()
    {
        string typeName = typeof(T).FullName ?? typeof(T).Name;
        Span<byte> name = stackalloc byte[Encoding.UTF8.GetMaxByteCount(Name.Length)];
        Span<byte> type = stackalloc byte[Encoding.UTF8.GetMaxByteCount(typeName.Length)];
        int nameLength = Encoding.UTF8.GetBytes(Name, name);
        int typeLength = Encoding.UTF8.GetBytes(typeName, type);
        fixed (byte* nameBytes = name)
        fixed (byte* typeBytes = type)
            return Native.EventIdOf(nameBytes, nameLength, typeBytes, typeLength);
    }
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
        Span<byte> utf8 = stackalloc byte[Encoding.UTF8.GetMaxByteCount(name.Length)];
        int length = Encoding.UTF8.GetBytes(name, utf8);
        bool accepted;
        fixed (byte* bytes = utf8)
            accepted = Native.RegisterScriptEvent(bytes, length, sizeof(T), ScriptComponents.AlignmentOf<T>());
        if (!accepted)
            throw new InvalidOperationException($"The engine did not accept the event {name}: events are reported only while a script assembly loads, and must have a plausible size.");
    }
}

/// <summary>
/// The events of one type that can be read now: this frame's so far and the last frame's, the same window C++
/// <c>ReadEvents</c> gives. It points at the engine's queue, so it is a <c>ref struct</c> good for the system that took it, and
/// it stays valid for that whole system because sends are held until the system returns.
/// </summary>
public readonly unsafe ref struct EventReader<T> where T : unmanaged, IEvent
{
    private readonly T* data;

    internal EventReader(T* data, int count)
    {
        this.data = data;
        Count = count;
    }

    /// <summary>How many events there are.</summary>
    public int Count { get; }

    /// <summary>The event at <paramref name="index"/>, oldest first.</summary>
    public ref readonly T this[int index]
    {
        get
        {
            if ((uint)index >= (uint)Count)
                throw new ArgumentOutOfRangeException(nameof(index));
            return ref data[index];
        }
    }

    public Enumerator GetEnumerator() => new(data, Count);

    public ref struct Enumerator
    {
        private readonly T* data;
        private readonly int count;
        private int index;

        internal Enumerator(T* data, int count)
        {
            this.data = data;
            this.count = count;
            index = -1;
        }

        /// <summary>The current event, by readonly reference (a <c>var</c> loop variable copies it).</summary>
        public readonly ref readonly T Current => ref data[index];

        public bool MoveNext() => ++index < count;
    }
}

public readonly partial struct SystemContext
{
    /// <summary>Sending and reading events.</summary>
    public EventsView Events => default;

    public readonly unsafe struct EventsView
    {
        /// <summary>
        /// Sends an event. It reaches the queue when the system returns: the sender does not read its own send, and the
        /// systems after it do.
        /// </summary>
        /// <exception cref="InvalidOperationException">The engine does not know the event.</exception>
        public void Send<T>(in T value) where T : unmanaged, IEvent
        {
            int code;
            fixed (T* pointer = &value)
                code = Native.EventSend(EventIds<T>.Id, pointer);
            if (code != 0)
                throw new InvalidOperationException(code == -6
                    ? $"Cannot send {typeof(T).Name}: the engine has no queue for it."
                    : $"Cannot send {typeof(T).Name}: the engine refused it (code {code}).");
        }

        /// <summary>The events of this type that can be read now. Sends made by this system are not in it.</summary>
        /// <exception cref="InvalidOperationException">The engine does not know the event.</exception>
        public EventReader<T> Read<T>() where T : unmanaged, IEvent
        {
            nuint data = 0;
            int count = Native.EventRead(EventIds<T>.Id, &data);
            if (count < 0)
                throw new InvalidOperationException($"Cannot read {typeof(T).Name}: the engine has no queue for it.");
            return new EventReader<T>((T*)data, count);
        }
    }
}
