namespace Fenrir.Scripting;

// The engine's own native functions: the built-in scripting extension. Each method is a native function named
// "Fenrir.Scripting.Native.<Method>" (see ScriptingExtension in FenrirScripting), and the source generator writes its body
// and the function pointer field the host binds when it shares this assembly. Sharing fails if a method has no C++
// function or a different signature. Script authors never see this class: every function has a friendly wrapper next to
// it (Log, Time, Entity) that hides the pointers, UTF-8 and lengths. To expose a function from another package, or to add
// one here, see "Exposing a native function" in the package README.
//
// When no Scripting is running a function is not called, and C# gets its "not running" value: -1 from ComponentIdOf,
// EventIdOf, EventRead, QueryCount and QueryFill, the refusal code -5 from EntityDestroy, ComponentAdd, ComponentRemove and
// EventSend, uint.MaxValue from EntityCreate, and zero or false from the rest. (C# only runs while one is, so this is
// the contract, not something a script sees.)
internal static unsafe partial class Native
{
    /// <summary>Log(level, utf8, byteCount): level 0 Info, 1 Warn, 2 Error.</summary>
    [NativeFunction]
    public static partial void Log(int level, byte* text, int length);

    [NativeFunction]
    public static partial float TimeDeltaTime();

    [NativeFunction]
    public static partial float TimeTickRate();

    /// <summary>Whether the entity value (the engine full entity value, version bits included) is alive in the active scene.</summary>
    [NativeFunction]
    public static partial bool EntityIsValid(uint entity);

    // The world transform (FenrirECS GetWorldTransform / SetWorldTransform), composed through the parents on each call.
    // Both return false, touching nothing, when the entity is not alive or has no Transform.
    [NativeFunction]
    public static partial bool EntityWorldTransform(uint entity, out Transform world);

    [NativeFunction]
    public static partial bool EntitySetWorldTransform(uint entity, in Transform world);

    // The engine's registry of native components. A component is found by the name it was registered under, and
    // that name is only good for the registry generation it was looked up in: a new Scripting has a new registry.
    //   ComponentIdOf(name, nameBytes, typeName, typeBytes): the id of the component registered under that name for the C#
    //   type with that full name; -1 when nothing is registered under the name (or nothing is running), -3 when it is
    //   registered for a different C# type (so a script cannot claim a native component with a struct of its own).
    //   RegistryGeneration(): changes every time a registry is created.
    //   ComponentGet(id, entity): a pointer to the entity's component in the engine's storage, or null when the entity
    //   is not alive or does not have it. The pointer is good until the engine next changes its storage.
    //   ComponentHas(id, entity): whether the entity is alive and has the component.
    [NativeFunction]
    public static partial int ComponentIdOf(byte* name, int nameBytes, byte* typeName, int typeBytes);

    /// <summary>RegisterScriptComponent(utf8Name, nameBytes, size, alignment): a script assembly reports a component type it defines, while it loads. False when nothing is loading.</summary>
    [NativeFunction]
    public static partial bool RegisterScriptComponent(byte* name, int nameBytes, int size, int alignment);

    [NativeFunction]
    public static partial uint RegistryGeneration();

    [NativeFunction]
    public static partial void* ComponentGet(int id, uint entity);

    [NativeFunction]
    public static partial bool ComponentHas(int id, uint entity);

    // Structural changes. None of them touches the engine's storage: each is recorded and played back, in order, when the
    // native code that called into the script gets control back, so a Ref<T> taken before stays valid.
    //   EntityCreate(): a new entity value, alive and usable at once (uint.MaxValue if there is no active scene).
    //   EntityDestroy(entity), ComponentAdd(id, entity, value), ComponentRemove(id, entity): 0 when recorded, otherwise the
    //   reason it was refused: -1 the entity is not alive, -2 no such component, -3 the component is read-only, -4 the
    //   engine requires every entity to have it, -5 scripting is not running. `value` is the component's bytes, or null for a default one.
    [NativeFunction]
    public static partial uint EntityCreate();

    [NativeFunction]
    public static partial int EntityDestroy(uint entity);

    [NativeFunction]
    public static partial int ComponentAdd(int id, uint entity, void* value);

    [NativeFunction]
    public static partial int ComponentRemove(int id, uint entity);

    // Events. Each is a queue the engine owns (the same queue C++ SendEvent/ReadEvents use for a native event).
    //   EventIdOf(name, nameBytes, typeName, typeBytes): like ComponentIdOf, for events.
    //   RegisterScriptEvent(utf8Name, nameBytes, size, alignment): a script assembly reports an event it defines, while it loads.
    //   EventSend(id, event): records an event to send when the system returns; 0 when recorded, otherwise a refusal code
    //   (-5 scripting is not running, -6 the event has no queue).
    //   EventRead(id, out data): how many events can be read (this frame's and last frame's) and where they are; negative if the
    //   event is unknown. The address is good until the system returns.
    [NativeFunction]
    public static partial int EventIdOf(byte* name, int nameBytes, byte* typeName, int typeBytes);

    [NativeFunction]
    public static partial bool RegisterScriptEvent(byte* name, int nameBytes, int size, int alignment);

    [NativeFunction]
    public static partial int EventSend(int id, void* value);

    [NativeFunction]
    public static partial int EventRead(int id, nuint* data);

    // Plays back (ChangesApply, returning how many changes were applied) or drops (ChangesDiscard) the structural changes
    // recorded since the last time. The system runner calls them after each system.
    [NativeFunction]
    public static partial int ChangesApply();

    [NativeFunction]
    public static partial void ChangesDiscard();

    // Queries. A row is the entity followed by the address of its component for each id, in order.
    //   QueryCount(ids, count): how many entities have every component; negative if an id is unknown.
    //   QueryFill(ids, count, rows, capacity): writes up to `capacity` rows and returns how many; negative if an id is unknown.
    // A query is these two calls however many entities match.
    [NativeFunction]
    public static partial int QueryCount(int* ids, int count);

    [NativeFunction]
    public static partial int QueryFill(int* ids, int count, nuint* rows, int capacity);
}
