using Fenrir.Math;
using Fenrir.Scripting;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace ScriptingDemo.Scripts;

// A component defined in C#: no C++ type, nothing registered anywhere. The SDK's build step reports it to the engine when this
// assembly loads, and the engine stores it beside Transform and Name.
public struct Spin : IComponent
{
    /// <summary>How fast the entity turns about the Y axis; negative turns the other way.</summary>
    public float RadiansPerSecond;

    /// <summary>Where it has got to. It is kept in the component, so it survives a hot reload of this assembly.</summary>
    public float Angle;
}

// Gives the demo's boxes a Spin. The entities are made when the scene is entered, after scripts load, so this looks for
// them every frame instead of in OnCreate, and spins them each time the scene is entered again (PageDown) and spawns new ones.
[System(Phase.Update, Order = 0)]
public sealed class SpinSetup : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        int found = 0;
        int added = 0;
        foreach (var (entity, name) in context.Query<Name>())
        {
            // "Box 1" and "Box 2", not their parent "Boxes".
            if (!name.Text.StartsWith("Box ", StringComparison.Ordinal))
                continue;

            if (!entity.Has<Spin>())
            {
                // Added when the system returns, so the Spinner below sees it from the next frame.
                entity.Add(new Spin { RadiansPerSecond = found % 2 == 0 ? 1.0f : -0.5f });
                added++;
            }
            found++;
        }

        if (added > 0)
            context.Log.Info($"Spinning {found} entities from C#. Press R to reverse them, Space for sparks.");
    }
}

// Turns every entity that has a Transform (an engine component) and a Spin (this assembly's own).
[System(Phase.Update, Order = 10)]
public sealed class Spinner : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        float deltaTime = context.Time.DeltaTime;
        foreach (var (entity, transform, spin) in context.Query<Transform, Spin>())
        {
            spin.Angle += spin.RadiansPerSecond * deltaTime;
            float half = spin.Angle * 0.5f;
            // Writes straight into the engine's Transform, which the renderer reads this frame.
            transform.Rotation = new Quaternion(0, MathF.Sin(half), 0, MathF.Cos(half));
        }
    }
}
