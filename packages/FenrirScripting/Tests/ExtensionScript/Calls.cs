using Fenrir.Scripting;
using Fenrir.TestExtension;

namespace Fenrir.TestExtensionScript;

// What the tests call, through Iris, to use the test extension from a script.
public static class Calls
{
    public static int Add(int left, int right) => Gadgets.Add(left, right);

    public static uint EchoEntity(uint entity) => Gadgets.EchoEntity(new Entity(entity)).Value;

    public static float HeightOf(uint entity) => Gadgets.HeightOf(new Entity(entity));

    public static void Lift(uint entity, float by) => Gadgets.Lift(new Entity(entity), by);

    // The Transform height as C# reads it from the engine's storage.
    public static float ScriptHeightOf(uint entity) => new Entity(entity).Get<Transform>().Value.Position.Y;

    public static int Label(string text, int number) => Gadgets.Label(text, number);

    public static int ChargeLevel(uint entity) => new Entity(entity).Get<Charge>().Value.Level;
}
