using Fenrir.Scripting;

namespace DemoScripts;

// READING THE GAMEPAD
//
// The DemoApp owns the gamepad, which C# cannot reach. DemoGamepad is the first connected one, declared in C++
// (examples/DemoApp/src/DemoGamepad.hpp) and shared as "Demo.DemoGamepad": put one on an entity in the scene and a C++
// system writes the pad into it every frame, before the ticks, while the scene is active. It is all zero while no pad
// is connected, so unplugging one releases everything it held.
[NativeComponent("Demo.DemoGamepad")]
public struct DemoGamepad : IComponent
{
    public int Connected;
    public Vector2 LeftStick;  // Each axis [-1, 1]: +X right, +Y down.
    public Vector2 RightStick;
    public float LeftTrigger;  // [0, 1]: 0 released.
    public float RightTrigger;
    public uint Buttons;       // Bit n is held GLFW gamepad button n: A is bit 0, Back bit 6.

    public readonly bool Held(GamepadButton button) => (Buttons & (1u << (int)button)) != 0;
}

/// <summary>GLFW's standard gamepad buttons, each DemoGamepad.Buttons's bit of that number.</summary>
public enum GamepadButton
{
    A = 0,
    B = 1,
    X = 2,
    Y = 3,
    LeftBumper = 4,
    RightBumper = 5,
    Back = 6,
    Start = 7,
    Guide = 8,
    LeftThumb = 9,
    RightThumb = 10,
    DpadUp = 11,
    DpadRight = 12,
    DpadDown = 13,
    DpadLeft = 14,
}

/// <summary>Two floats, laid out exactly like glm::vec2, which the engine's contract has no type for.</summary>
public struct Vector2
{
    public float X, Y;

    public Vector2(float x, float y)
    {
        X = x;
        Y = y;
    }
}
