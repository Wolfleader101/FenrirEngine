using Fenrir.Scripting;

namespace DemoScripts;

// SHARING A COMPONENT BETWEEN C++ AND C#
//
// Bob is declared in C++ (examples/DemoApp/src/ScriptingDemo.hpp) and registered as "Demo.Bob". This struct mirrors it field for
// field, and [NativeComponent] says which C++ component it is. The engine checks the two layouts when the script loads and refuses
// to load on a mismatch, naming the field. After that there is only one copy of the data: C++ and C# read and write the same bytes
// in the ECS, with nothing converted or copied.
[NativeComponent("Demo.Bob")]
public struct Bob : IComponent
{
    public float Amplitude; // Set by C++ when it creates the component.
    public float Speed;     // Set by C++.
    public float Phase;     // Written here, every frame; C++ reads it back and shows it in the entity's name.
    public float BaseY;     // Set by C++.
}

// Moves every entity that has a Transform (an engine component) and a Bob (a C++ component of the app's) up and down.
[System(Phase.Update, Order = 20)]
public sealed class Bobber : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        float deltaTime = context.Time.DeltaTime;
        foreach (var (entity, transform, bob) in context.Query<Transform, Bob>())
        {
            bob.Phase += bob.Speed * deltaTime;
            transform.Position.Y = bob.BaseY + bob.Amplitude * MathF.Sin(bob.Phase);
        }
    }
}
