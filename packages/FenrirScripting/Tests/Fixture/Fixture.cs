using System.Runtime.Loader;
using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// A script assembly that exercises the engine API from C#. The tests call these static methods through Iris and look
// at what the engine saw.
public static class Fixture
{
    public static int Answer()
    {
#if VARIANT2
        return 43;
#else
        return 42;
#endif
    }

    // Log: all three levels, then text that must not be taken for a format string and is not ASCII.
    public static void LogAll()
    {
        Log.Info("hello from C#");
        Log.Warn("careful now");
        Log.Error("boom");
        Log.Info("pi \u03c0 {0} {} {name}");
        Log.Info(new string('x', 2000));
        Log.Info(null);
    }

    public static float DeltaTime() => Time.DeltaTime;

    public static float TickRate() => Time.TickRate;

    public static bool IsValid(uint value) => new Entity(value).IsValid;

    // The contract came from the host's shared copy, in the default load context.
    public static bool ContractIsShared() =>
        AssemblyLoadContext.GetLoadContext(typeof(Entity).Assembly) == AssemblyLoadContext.Default &&
        AssemblyLoadContext.GetLoadContext(typeof(Fixture).Assembly) != AssemblyLoadContext.Default;

    // Where the contract was loaded from: the host's shared copy, wherever a script may have deployed its own.
    public static string ContractLocation() => typeof(Entity).Assembly.Location;

    // An identity for the contract assembly instance: equal across loads of this assembly when it is one shared copy.
    public static int ContractIdentity() => System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(typeof(Entity).Assembly);
}
