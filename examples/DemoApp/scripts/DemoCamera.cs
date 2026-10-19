using Fenrir.Scripting;

namespace DemoScripts;

// DRIVING THE CAMERA
//
// The DemoApp has one camera, which C# cannot reach. DemoCamera is where a scene wants it, declared in C++
// (examples/DemoApp/src/DemoCamera.hpp) and shared as "Demo.DemoCamera": put one on an entity in the scene and, while
// Active is non-zero, a C++ system copies it into the camera every frame, after the C# Update systems, so it wins over
// the free-fly camera. Setting Active to 0 hands the camera back to the free-fly controls.
[NativeComponent("Demo.DemoCamera")]
public struct DemoCamera : IComponent
{
    public Vector3 Position;
    public float Yaw;   // Degrees about world +Y: -90 looks along -Z.
    public float Pitch; // Degrees above the horizon.
    public int Active;
}
