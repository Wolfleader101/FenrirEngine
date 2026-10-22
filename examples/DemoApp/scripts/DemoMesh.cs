using Fenrir.Math;
using Fenrir.Scripting;

namespace DemoScripts;

// MAKING THINGS VISIBLE WITHOUT THE RENDERER
//
// The renderer draws an entity from its Model and Material, which C# cannot (and should not) build. DemoMesh is the app's
// plain-data stand-in, declared in C++ (examples/DemoApp/src/DemoMesh.hpp) and shared as "Demo.DemoMesh": add one and a
// C++ system gives the entity the model Kind names, in one shaded colour, before the frame is drawn. Both models are one
// unit across, so the entity's Transform scale is its size. Colour is read every frame, so writing it recolours the
// entity at once; Kind is read once, when the entity is first drawn.

public enum MeshKind : int
{
    Cube = 0,
    Sphere = 1,
}

[NativeComponent("Demo.DemoMesh")]
public struct DemoMesh : IComponent
{
    public MeshKind Kind;
    public Vector3 Colour; // RGB, 0 to 1.
}
