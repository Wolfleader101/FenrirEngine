namespace Fenrir.Scripting.Generators;

/// <summary>
/// The names of the types the generators write into a script assembly, which the engine finds by name when a generation
/// loads: the one place in the SDK that spells them. The engine spells the same names once, in
/// <c>packages/FenrirScripting/src/GeneratedNames.hpp</c>; nothing reads one from the other (ADR-0042, amended 2026-10-28).
/// The end-to-end scripting tests load real generated assemblies, so a drift between the two files fails them.
/// </summary>
internal static class GeneratedNames
{
    /// <summary>The namespace every generated type is in.</summary>
    public const string Namespace = "Fenrir.Scripting.Generated";

    /// <summary>The system host: makes the script's systems and runs them, one per script assembly.</summary>
    public const string SystemHost = "SystemHost";

    /// <summary><c>int PhaseMask()</c> on the system host: bit n is set when the script has a system in phase n.</summary>
    public const string PhaseMask = "PhaseMask";

    /// <summary><c>void OnCreate()</c> on the system host: every system's OnCreate, once the generation is loaded.</summary>
    public const string OnCreate = "OnCreate";

    /// <summary><c>void Run(int phase)</c> on the system host: the systems of one phase, in order.</summary>
    public const string Run = "Run";

    /// <summary><c>void OnDestroy()</c> on the system host: every system's OnDestroy, before the generation is released.</summary>
    public const string OnDestroy = "OnDestroy";

    /// <summary>The table that reports the script's components as the assembly loads.</summary>
    public const string ComponentTable = "ComponentTable";

    /// <summary>The table that reports the script's C#-only events as the assembly loads.</summary>
    public const string EventTable = "EventTable";

    /// <summary><c>static void Register()</c> on each table.</summary>
    public const string Register = "Register";

    /// <summary>The hint name of the source file holding the generated type <paramref name="type"/>.</summary>
    public static string HintName(string type) => Namespace + "." + type + ".g.cs";
}
