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

// How every Spin turns, on one entity of the scene's: reversed or not, and how fast. R reverses it and [ and ] step its
// speed (Controls.cs), and the Razor panel in Showcase's demo slot shows and changes both (ShowcasePanel.razor).
public struct SpinDial : IComponent
{
    public const float MinSpeed = 0.1f;
    public const float MaxSpeed = 10.0f;

    /// <summary>Nonzero turns every Spin the other way (an int: a component's fields are blittable, and bool is not).</summary>
    public int Reversed;
    /// <summary>What every Spin's rate is multiplied by, from MinSpeed to MaxSpeed.</summary>
    public float Speed;
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
        {
            // The dial, with the boxes: the scene's entities go when it is left, so each entry makes its own.
            bool dialled = false;
            foreach (var _ in context.Query<SpinDial>())
                dialled = true;
            if (!dialled)
                context.CreateEntity().Add(new SpinDial { Speed = 1.0f });
            context.Log.Info($"Spinning {found} entities from C#. Press R to reverse them, Space for sparks.");
        }
    }
}

// Turns every entity that has a Transform (an engine component) and a Spin (this assembly's own), as the SpinDial says.
[System(Phase.Update, Order = 10)]
public sealed class Spinner : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        float deltaTime = context.Time.DeltaTime;
        float rate = 1.0f;
        foreach (var (_, dial) in context.Query<SpinDial>())
            rate = dial.Reversed != 0 ? -dial.Speed : dial.Speed;
        foreach (var (entity, transform, spin) in context.Query<Transform, Spin>())
        {
            spin.Angle += spin.RadiansPerSecond * rate * deltaTime;
            float half = spin.Angle * 0.5f;
            // Writes straight into the engine's Transform, which the renderer reads this frame.
            transform.Rotation = new Quaternion(0, MathF.Sin(half), 0, MathF.Cos(half));
        }
    }
}
