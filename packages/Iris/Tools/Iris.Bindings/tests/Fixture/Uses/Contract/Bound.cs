namespace Fixture.Common;

// The Common binding's own C# type for its bound type (Common.binding.toml, [bound-types]), written by hand as a binding
// writes its vector. Native/CommonBound.hpp maps it. The fixture's natives take it as this type (Fixture.binding.toml uses
// this binding), without the fixture's own description describing it.

/// <summary>Stands for <c>Fixture::Angles</c>.</summary>
public struct Angles
{
    public float Pitch, Yaw;
}
