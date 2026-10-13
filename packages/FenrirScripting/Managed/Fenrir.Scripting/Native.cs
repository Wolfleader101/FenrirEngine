// Iris assigns every field below when the host shares this assembly, so the compiler never sees an assignment.
#pragma warning disable CS0649

namespace Fenrir.Scripting;

// The engine's native functions. Each public static field is bound by Iris when the host shares this assembly,
// to the function the engine registered under the name "Fenrir.Scripting.Native.<Field>", and sharing fails if one is
// missing or has a different signature. Script authors never see this class: every function has a friendly wrapper
// next to it (Log, Time, Entity) that hides the pointers, UTF-8 and lengths. To add an engine API, see the
// package README.
internal static unsafe class Native
{
    // Log(level, utf8, byteCount): level 0 Info, 1 Warn, 2 Error.
    public static delegate* unmanaged[Cdecl]<int, byte*, int, void> Log;

    public static delegate* unmanaged[Cdecl]<float> TimeDeltaTime;
    public static delegate* unmanaged[Cdecl]<float> TimeTickRate;

    // Whether the entity value (the engine full entity value, version bits included) is alive in the active scene.
    public static delegate* unmanaged[Cdecl]<uint, bool> EntityIsValid;

    // The engine's registry of native components. A component is found by the name it was registered under, and
    // that name is only good for the registry generation it was looked up in: a new Scripting has a new registry.
    //   ComponentIdOf(name, nameBytes, typeName, typeBytes): the id of the component registered under that name for the C#
    //   type with that full name; -1 when nothing is registered under the name, -3 when it is registered for a different
    //   C# type (so a script cannot claim a native component with a struct of its own).
    //   RegistryGeneration(): changes every time a registry is created.
    //   ComponentGet(id, entity): a pointer to the entity's component in the engine's storage, or null when the entity
    //   is not alive or does not have it. The pointer is good until the engine next changes its storage.
    //   ComponentHas(id, entity): whether the entity is alive and has the component.
    public static delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> ComponentIdOf;

    // RegisterScriptComponent(utf8Name, nameBytes, size, alignment): a script assembly reports a component type it defines,
    // while it loads. False when nothing is loading.
    public static delegate* unmanaged[Cdecl]<byte*, int, int, int, bool> RegisterScriptComponent;
    public static delegate* unmanaged[Cdecl]<uint> RegistryGeneration;
    public static delegate* unmanaged[Cdecl]<int, uint, void*> ComponentGet;
    public static delegate* unmanaged[Cdecl]<int, uint, bool> ComponentHas;

    // Structural changes. None of them touches the engine's storage: each is recorded and played back, in order, when the
    // native code that called into the script gets control back, so a Ref<T> taken before stays valid.
    //   EntityCreate(): a new entity value, alive and usable at once (uint.MaxValue if there is no active scene).
    //   EntityDestroy(entity), ComponentAdd(id, entity, value), ComponentRemove(id, entity): 0 when recorded, otherwise the
    //   reason it was refused: -1 the entity is not alive, -2 no such component, -3 the component is read-only, -4 the
    //   engine requires every entity to have it, -5 scripting is not running. `value` is the component's bytes, or null for a default one.
    public static delegate* unmanaged[Cdecl]<uint> EntityCreate;
    public static delegate* unmanaged[Cdecl]<uint, int> EntityDestroy;
    public static delegate* unmanaged[Cdecl]<int, uint, void*, int> ComponentAdd;
    public static delegate* unmanaged[Cdecl]<int, uint, int> ComponentRemove;
}
