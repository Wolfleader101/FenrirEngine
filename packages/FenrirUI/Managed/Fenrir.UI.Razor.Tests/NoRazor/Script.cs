using Fenrir.Scripting;

namespace NoRazor;

// Something for the scripting SDK's generator to generate from: a component and a system.
public struct Health : IComponent
{
    public float Value;
}

[System(Phase.Update)]
public sealed class Regenerate : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
    }
}
