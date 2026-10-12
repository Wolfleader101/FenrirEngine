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

    // Whether the entity value (the full entt value, version bits included) is alive in the active scene.
    public static delegate* unmanaged[Cdecl]<uint, bool> EntityIsValid;
}
