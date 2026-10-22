using Fenrir.Math;

namespace Fenrir.Scripting;

/// <summary>An entity's place in the world: the engine's <c>Fenrir::Transform</c>, shared with native code.</summary>
[NativeComponent("Fenrir.Transform")]
public struct Transform : IComponent
{
    public Vector3 Position;
    public Quaternion Rotation;
    public Vector3 Scale;
}
